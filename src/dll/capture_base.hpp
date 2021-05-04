#pragma once

#include <cstddef>

///
/// Interface for writing PCM samples.
///
struct outformat_base
{
    virtual ~outformat_base() = default;

    /// Sets up the format. This should only be called once.
    virtual void setup(unsigned frequency, unsigned bits, unsigned channels) = 0;

    /// Writes nbytes bytes of raw samples.
    virtual void write_pcm(const void *src, size_t nbytes) = 0;
};

///
/// Interface for capturing device.
///
struct capture_base
{
    virtual ~capture_base() = default;
};