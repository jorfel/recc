#pragma once

#include <ostream>

#include "capture_base.hpp"

///
/// Writes raw PCM samples to output stream.
///
class pcm_file : public outformat_base
{
public:
    pcm_file(std::ostream &out) : stream(&out) {}

    void setup(unsigned frequency, unsigned bits, unsigned channels) override {}

    void write_pcm(const void *src, size_t nbytes) override
    {
        stream->write((const char*)src, nbytes);
    }

private:
    std::ostream *stream;
};