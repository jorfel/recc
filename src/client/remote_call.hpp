#pragma once

#include <array>
#include <memory>
#include <string>
#include <Windows.h>

#include "../handle_holder.hpp"
#include "../win32_error.hpp"


/// Utility for releasing stuff allocated with VirtualAllocEx
struct valloc_releaser
{
    HANDLE hprocess;
    void operator()(void *p) { VirtualFreeEx(hprocess, p, 0, MEM_RELEASE); }
};

using valloc_ptr = std::unique_ptr<void, valloc_releaser>;


/// Emits assembly code that sets up a function's arguments, depending on the type.
/// Integral arguments will be moved into registers, strings will get their
/// address moved instead and their actual data will be emitted by emit_arguments_data().
template<size_t Idx = 0, class Arg, class... Args>
void emit_arguments(asmjit::x86::Assembler &as, const Arg &arg, const Args &...args)
{
    using namespace asmjit::x86;
    static const Gpq arg_registers[] = { rcx, rdx, r8, r9 };

    static_assert(Idx < std::size(arg_registers), "too many arguments");

    if constexpr(!std::is_integral_v<Arg>) //create label to reference data in memory, assigned later
        as.lea(arg_registers[Idx], ptr(as.newNamedLabel(("arg" + std::to_string(Idx)).c_str())));
    else
        as.mov(arg_registers[Idx], arg); //immediate constant

    if constexpr(sizeof...(Args) > 0)
        emit_arguments<Idx + 1>(as, args...);
}

/// Emits string data and a label referencing it.
template<size_t Idx = 0, class Arg, class... Args>
void emit_arguments_data(asmjit::x86::Assembler &as, const Arg &arg, const Args &...args)
{
    if constexpr(!std::is_integral_v<Arg>) //put label defined above at current memory location and emit data
    {
        as.bind(as.labelByName(("arg" + std::to_string(Idx)).c_str()));
        as.embed(arg.data(), arg.size() * sizeof(*arg.data()));

        char terminator[sizeof(*arg.data())] = { 0 }; //put null-terminator
        as.embed(terminator, sizeof(terminator));
    }

    if constexpr(sizeof...(Args) > 0)
        emit_arguments_data<Idx + 1>(as, args...);
}


/// Loads a DLL (if not already loaded) into a process and calls a function with arguments.
/// Unloads DLL when finished if unloadafter is true.
/// Returns the handle to the remote thread.
/// x64 calling-convention: Return value in rax, arguments in rcx, rdx, r8, r9 and 32 bytes shadow space after return address
template<size_t MaxAlloc=0x2000, class... Args>
static handle_holder dll_call(HANDLE hprocess, bool unloadafter, std::wstring_view dllpath, std::string_view funcname, const Args &...args)
{
    using namespace asmjit;
    using namespace asmjit::x86;

    //allocate memory within other process' space
    valloc_ptr remotebuff(VirtualAllocEx(hprocess, nullptr, MaxAlloc, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READ), valloc_releaser{ hprocess });
    if(remotebuff == nullptr)
        throw win32_error(GetLastError(), "VirtualAllocEx failed.");

    //assemble code
    Environment env;
    env.setArch(Environment::kArchX64);

    CodeHolder code;
    code.init(env, std::uintptr_t(remotebuff.get())); //initialize with base address

    Assembler as(&code);

    //align rsp
    as.and_(rsp, 0xFFFFFFFFFFFFFFF0);
    as.sub(rsp, 32); //shadow space

    //call GetModuleHandleW
    as.lea(rcx, ptr(as.newNamedLabel("dllpath"))); //arg1 = dllpath
    as.mov(rax, &GetModuleHandleW); //functions in DLLs have the same addresses accross all spaces
    as.call(rax);
    as.test(rax, rax);
    as.jnz(as.newNamedLabel("findfunc"));

    //call LoadLibraryW if not loaded
    as.lea(rcx, ptr(as.labelByName("dllpath"))); //arg1 = dllpath
    as.mov(rax, &LoadLibraryW);
    as.call(rax);
    as.test(rax, rax);
    as.jz(as.newNamedLabel("fail"));

    //call GetProcAddress
    as.bind(as.labelByName("findfunc"));
    as.mov(r12, rax); //r12 = module handle (r12 is a non-clobbered register)
    as.mov(rcx, rax); //arg1 = module handle
    as.lea(rdx, ptr(as.newNamedLabel("funcname"))); //arg2 = funcname
    as.mov(rax, &GetProcAddress);
    as.call(rax);
    as.test(rax, rax);
    as.jz(as.labelByName("fail"));

    //call function
    if constexpr(sizeof...(Args) > 0)
        emit_arguments(as, args...);
    as.call(rax);
    as.jmp(as.newNamedLabel("exit"));

    //error
    as.bind(as.labelByName("fail"));
    as.mov(rax, &GetLastError);
    as.call(rax);

    //exit
    as.bind(as.labelByName("exit"));
    as.mov(rsi, rax); //save last return value in rsi (rsi is a non-clobbered register)

    if(unloadafter)
    {
        as.mov(rcx, r12); //arg1 = module handle
        as.mov(rax, &FreeLibrary);
        as.call(rax);
    }

    //place exit-stub on stack
    as.mov(byte_ptr(rsp, 0), 0x48); //mov rcx, rsi
    as.mov(byte_ptr(rsp, 1), 0x89);
    as.mov(byte_ptr(rsp, 2), 0xF1);
    as.mov(byte_ptr(rsp, 3), 0x48); //mov eax, &ExitThread
    as.mov(byte_ptr(rsp, 4), 0xB8);
    as.mov(rax, &ExitThread);
    as.mov(ptr(rsp, 5), rax);
    as.mov(byte_ptr(rsp, 13), 0xFF); //jmp rax
    as.mov(byte_ptr(rsp, 14), 0xE0);
    as.mov(rdi, rsp); //rdi = pointer to this stub (nonvolatile register)
    as.sub(rsp, 32); //new shadow space

    as.mov(rcx, rdi); //call VirtualProtect to make the stack executable
    as.mov(rdx, 32);
    as.mov(r8, PAGE_EXECUTE_READWRITE);
    as.lea(r9, ptr(rsp, 16)); //lpflOldProtect points to dummy memory
    as.mov(rax, &VirtualProtect);
    as.call(rax);

    as.mov(rcx, remotebuff.get()); //free memory
    as.mov(rdx, 0);
    as.mov(r8, MEM_RELEASE);
    as.mov(rax, &VirtualFree);
    as.push(rdi);
    as.jmp(rax); //jump instead of call, so VirtualFree will return to our stub on the executable stack
    
    //data
    as.bind(as.labelByName("dllpath"));
    as.embed(dllpath.data(), sizeof(wchar_t) * dllpath.size());
    as.embedUInt16(0);

    as.bind(as.labelByName("funcname"));
    as.embed(funcname.data(), funcname.size());
    as.embedUInt8(0);

    if constexpr(sizeof...(Args) > 0)
        emit_arguments_data(as, args...);

    CodeBuffer &buffer = code.textSection()->buffer();

    //write code
    if(WriteProcessMemory(hprocess, remotebuff.get(), buffer.data(), buffer.size(), nullptr) == FALSE)
        throw win32_error(GetLastError(), "WriteProcessMemory failed.");

    //create remote thread and execute generated code
    handle_holder hthread(CreateRemoteThread(hprocess, nullptr, 0, LPTHREAD_START_ROUTINE(remotebuff.get()), 0, 0, nullptr));
    if(!hthread)
        throw win32_error(GetLastError(), "CreateRemoteThread failed.");

    remotebuff.release(); //remote thread will free its memory by itself
    return hthread;
}
