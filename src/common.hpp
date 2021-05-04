#pragma once

#include <utility>
#include <stdexcept>
#include <handleapi.h>

/// Automatically calls CloseHandle.
struct handle_holder
{
    handle_holder(const handle_holder &) = delete;
    handle_holder &operator=(const handle_holder &) = delete;

    HANDLE handle = 0;

    handle_holder() = default;
    explicit handle_holder(HANDLE h) : handle(h) {}
    handle_holder(handle_holder &&rhs) noexcept : handle(rhs.handle) { rhs.handle = 0; }

    handle_holder &operator=(handle_holder &&rhs) noexcept
    {
        close();
        handle = rhs.handle;
        rhs.handle = 0;
        return *this;
    }

    void close() noexcept
    {
        if(handle != INVALID_HANDLE_VALUE && handle != 0)
            CloseHandle(handle);
        handle = 0;
    }

    ~handle_holder() noexcept { close();  }

    HANDLE get() const { return handle; }
    HANDLE *ptr() { return &handle; }
    bool operator!() const { return handle == INVALID_HANDLE_VALUE || handle == 0; }
    operator bool() const { return !!handle; }

};


/// Contains (Win32-) error code and message.
struct precise_error : public std::runtime_error
{
    HRESULT code;
    precise_error(HRESULT code, std::string message) : code(code), runtime_error(std::move(message)) {}
};