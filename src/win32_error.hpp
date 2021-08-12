#pragma once

#include <winerror.h>
#include <stdexcept>


/// Contains (Win32-) error code and and a custom message.
/// We don't use std::system_error here, because a HRESULT might not fit into an int.
struct win32_error : public std::runtime_error
{
    HRESULT code;
    win32_error(HRESULT code, std::string message) : code(code), runtime_error(std::move(message)) {}
};