#pragma once

#include <mutex>
#include <ostream>
#include <cstddef>
#include <cstdint>

#include <dsound.h>

#include "capture_base.hpp"

///
/// Hooks into DirectSound by replacing IDirectSoundBuffer::unlock in the VTable.
/// 
class capture_dsound : public capture_base
{
public:
    capture_dsound(capture_dsound&&) = delete;

    capture_dsound(std::mutex &lock, std::ostream &log, outformat_base &out);
    ~capture_dsound();

private:
    std::mutex *lock;
    std::ostream *log;
    bool output_setup;
    outformat_base *output;

    static HRESULT WINAPI hook_unlock(IDirectSoundBuffer*, LPVOID, DWORD, LPVOID, DWORD);
    using unlock_func = decltype(hook_unlock);

    unlock_func **vtable;
    unlock_func *old_query, *old_unlock;

};