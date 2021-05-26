#include <array>
#include <iostream>
#include <filesystem>
#include <cxxopts.hpp>
#include <asmjit/x86.h>

#include <io.h>
#include <fcntl.h>
#include <Windows.h>

#include "../common.hpp"
#include "remote_call.hpp"
#include "signal_context.hpp"


/// Retrieves process handle from process id.
static handle_holder process_from_id(DWORD pid)
{
    handle_holder hprocess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
    if(!hprocess)
        throw precise_error(GetLastError(), "OpenProcess failed.");

    BOOL wow64 = TRUE;
    if(IsWow64Process(hprocess.get(), &wow64); wow64)
        return handle_holder();

    return hprocess;
}

/// Finds a window whose title contains cmptitle and returns its process handle or invalid handle when not found.
static handle_holder process_from_window(const std::wstring &cmptitle)
{
    std::pair<HWND, const std::wstring*> vars = { 0, &cmptitle };
    auto callback = [](HWND hwnd, LPARAM lparam) -> BOOL
    {
        auto *vars = (std::pair<HWND, const std::wstring*>*)lparam;
        std::wstring title;
        title.resize(GetWindowTextLengthW(hwnd));
        GetWindowTextW(hwnd, title.data(), title.size() + 1);

        if(title.find(*vars->second) != title.npos)
        {
            vars->first = hwnd;
            return FALSE;
        }

        return TRUE;
    };

    EnumWindows(callback, (LPARAM)&vars);

    if(vars.first == 0)
        return handle_holder();

    DWORD pid;
    if(GetWindowThreadProcessId(vars.first, &pid) == 0)
        throw precise_error(GetLastError(), "GetWindowThreadProcessId failed.");
    
    return process_from_id(pid);
}

/// utf8 -> utf16 conversion
static std::wstring utf82utf16(std::string_view src)
{
    std::wstring dest;
    dest.resize(MultiByteToWideChar(CP_UTF8, 0, src.data(), src.size(), nullptr, 0));
    MultiByteToWideChar(CP_UTF8, 0, src.data(), src.size(), dest.data(), dest.size());
    return dest;
}

/// Coroutine for creating a pipe and directing it to f.
static void_task print_pipe(signal_context &ctx, const std::wstring &pipe_path, FILE *f)
{
    handle_holder hpipe(CreateNamedPipeW(pipe_path.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, 1, 1024, 1024, 0, nullptr));
    if(!hpipe)
        throw precise_error(GetLastError(), "CreateNamedPipeW failed.");

    handle_holder hpipe_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if(!hpipe)
        throw precise_error(GetLastError(), "CreateEventW failed.");

    OVERLAPPED pipe_async = { 0 };
    pipe_async.hEvent = hpipe_event.get(); //overlapped operations will signal this event when finished

    if(!ConnectNamedPipe(hpipe.get(), &pipe_async) && GetLastError() != ERROR_IO_PENDING) //get notified when client connects
        throw precise_error(GetLastError(), "ConnectNamedPipe failed.");

    co_await handle_awaiter(ctx, hpipe_event.get()); //wait for client connection

    char read_buffer[1024];
    for(;;)
    {
        if(!ReadFile(hpipe.get(), read_buffer, sizeof(read_buffer), nullptr, &pipe_async))
        {
            auto err = GetLastError();
            if(err == ERROR_BROKEN_PIPE) //client disconnected
                break;
            else if(err != ERROR_IO_PENDING)
                throw precise_error(GetLastError(), "ReadFile failed.");
        }

        co_await handle_awaiter(ctx, hpipe_event.get()); //wait for read completion

        DWORD bytes_avail = 0;
        if(!GetOverlappedResult(hpipe.get(), &pipe_async, &bytes_avail, FALSE))
        {
            auto err = GetLastError();
            if(err == ERROR_BROKEN_PIPE) //client disconnected
                break;
            else
                throw precise_error(GetLastError(), "GetOverlappedResult failed.");
        }

        std::fwrite(read_buffer, 1, bytes_avail, f); //C-streams are faster
    }
}

/// Coroutine for main stuff.
static void_task capture(signal_context &ctx, HANDLE hprocess, const std::wstring &dllpath, const std::string &api, const std::string &format, const std::wstring &out_path, const std::wstring &log_path)
{
    //inject DLL and call recc_log first
    handle_holder hthread = dll_call(hprocess, false, dllpath, "recc_log", log_path);
    if(DWORD c = co_await thread_awaiter(ctx, hthread.get()); c != 0)
        throw precise_error(c, "Thread for recc_log reported failure.");

    //call recc_capture
    hthread = dll_call(hprocess, false, dllpath, "recc_capture", out_path, api, format);
    if(DWORD c = co_await thread_awaiter(ctx, hthread.get()); c != 0)
        throw precise_error(c, "Thread for recc_capture reported failure.");

    std::cerr << "Press any key to release ...\n";

    //wait for console keypress
    co_await console_awaiter(ctx);

    //release capture and free DLL
    hthread = dll_call(hprocess, true, dllpath, "recc_release");
    if(DWORD c = co_await thread_awaiter(ctx, hthread.get()); c != 0)
        throw precise_error(c, "Thread for recc_release reported failure.");
}

static int main2(int argc, char **argv)
{
    cxxopts::Options opts("recc", "Records audio output from another application.");

    opts.add_options()("p,pid", "Process ID of target application.", cxxopts::value<DWORD>(), "number");
    opts.add_options()("w,window", "Window title or part of it of a target application. Overwrites -p.", cxxopts::value<std::string>(), "text");
    opts.add_options()("a,api", "The audio API to use.", cxxopts::value<std::string>()->default_value("dsound"), "text");
    opts.add_options()("o,output", "Output wave file. Use -- for stdout.", cxxopts::value<std::string>()->default_value("./rec.wav"), "path");
    opts.add_options()("l,log", "Output log file. Use -- for stderr.", cxxopts::value<std::string>()->default_value("--"), "path");
    opts.add_options()("f,format", "Output format.", cxxopts::value<std::string>()->default_value("wav"), "text");

    auto optres = opts.parse(argc, argv);

    if(argc <= 1 || optres.count("window") + optres.count("pid") == 0)
    {
        std::cerr << opts.help();
        std::cerr << "Supported audio APIs:\n"
                  << "  dsound (DirectSound)\n"
                  << "Supported audio formats:\n"
                  << "  wav (RIFF WAVE)\n"
                  << "  pcm (raw PCM)\n"
                  << "Sampling information (frequency, bit depth, channels) depend on the output of the target application.\n";
        return 0;
    }

    //find target process
    handle_holder hprocess;
    if(optres.count("window") > 0)
        hprocess = process_from_window(utf82utf16(optres["window"].as<std::string>()));
    else
        hprocess = process_from_id(optres["pid"].as<DWORD>());

    if(!hprocess)
    {
        std::cerr << "There is no such (64-bit) process.\n";
        return -1;
    }

    //main loop
    signal_context main_loop;
    void_task data_pipe_task, log_pipe_task, captask;

    std::string out = optres["output"].as<std::string>();
    std::string log = optres["log"].as<std::string>();

    std::wstring out_path, log_path;
    if(out == "--")
    {
        out_path = L"\\\\.\\pipe\\recc" + std::to_wstring(GetProcessId(GetCurrentProcess()));
        data_pipe_task = print_pipe(main_loop, out_path, stdout);
        _setmode(fileno(stdout), _O_BINARY);
    }
    else
    {
        std::filesystem::path p = optres["output"].as<std::string>();
        if(!p.is_absolute())
            p = std::filesystem::absolute(p);
        out_path = p.wstring();
    }

    if(log == "--")
    {
        log_path = L"\\\\.\\pipe\\recc_log" + std::to_wstring(GetProcessId(GetCurrentProcess()));
        log_pipe_task = print_pipe(main_loop, log_path, stderr);
    }
    else
    {
        std::filesystem::path p = optres["log"].as<std::string>();
        if(!p.is_absolute())
            p = std::filesystem::absolute(p);
        log_path = p.wstring();
    }

    wchar_t exepath[MAX_PATH];
    auto flen = GetModuleFileNameW(0, exepath, MAX_PATH);
    if(flen == 0)
        throw precise_error(GetLastError(), "GetModuleFileNameW failed.");

    auto exepath_str = std::wstring(exepath, flen);
    exepath_str.erase(exepath_str.find_last_of(L'\\')+1);
    std::wstring dllpath = exepath_str + L"recc_dll.dll";

    captask = capture(main_loop, hprocess.get(), dllpath, optres["api"].as<std::string>(), optres["format"].as<std::string>(), out_path, log_path);
    main_loop.run();
    return 0;
}

int main(int argc, char **argv)
{
    try
    {
#ifndef NDEBUG
        argc = 3;
        char *argv[] = { (char*)"recc.exe", (char*)"-w", (char*)"xrns" };
#endif
        return main2(argc, argv);
    }
    catch(const precise_error &e)
    {
        std::cerr << e.what() << " (code 0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << std::make_unsigned_t<decltype(e.code)>(e.code) << ")" << std::endl;
    }
    catch(const std::exception &e)
    {
        std::cerr << e.what() << ".\n";
    }
    return -1;
}