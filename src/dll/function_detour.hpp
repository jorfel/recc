#include <cstring>
#include <Windows.h>

///
/// Hook that manipulates a function's code to execute a hook instead.
///
class function_detour
{
public:
    function_detour() = default;
    function_detour(function_detour&&) = delete;

    template<class Hooked, class Hook>
    void detour(Hooked hooked, Hook hook)
    {
        VirtualProtect((void*)hooked, sizeof(new_code), PAGE_EXECUTE_READWRITE, &old_func_protect);
        std::memcpy(old_code, (void*)hooked, sizeof(old_code));
        FlushInstructionCache(GetCurrentProcess(), (void*)hooked, sizeof(new_code));

        new_code[0] = 0x48; //mov rax, &hook
        new_code[1] = 0xB8;
        *(uintptr_t*)&new_code[2] = (uintptr_t)hook;
        new_code[10] = 0xFF; //jump rax
        new_code[11] = 0xB0;
    }

    /// Restores the original function (actually doesn't).
    ~function_detour() noexcept
    {
        if(hooked)
            VirtualProtect((void*)hooked, sizeof(new_code), old_func_protect, &old_func_protect);
    }

    /// Calls the original function with arguments.
    template<class Function, class... Args>
    auto call_old(Args &&...args) const noexcept
    {
        std::memcpy((void*)hooked, old_code, sizeof(old_code));
        FlushInstructionCache(GetCurrentProcess(), (void*)hooked, sizeof(old_code));

        auto ret = ((std::decay_t<Function>)hooked)(std::forward<Args>(args)...);

        std::memcpy((void*)hooked, new_code, sizeof(new_code));
        FlushInstructionCache(GetCurrentProcess(), (void*)hooked, sizeof(new_code));
        return ret;
    }

private:
    std::intptr_t hooked = 0;
    DWORD old_func_protect;
    char old_code[12];
    char new_code[12];
};
