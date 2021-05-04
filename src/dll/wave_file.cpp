#include "wave_file.hpp"

#include <cassert>
#include <ostream>
#include <cstdint>

template<class T>
std::ostream &serialize(std::ostream &o, const T &t)
{
	return o.write((const char*)&t, sizeof(t));
}

wave_file::wave_file(std::ostream &o) : total_length(0), stream(&o)
{
}

void wave_file::setup(unsigned frequency, unsigned bits, unsigned channels)
{
	stream->write("RIFF", 4);
	serialize(*stream, uint32_t(0)); //offset 0x04: file size - 8, will be fixed when closing file

	stream->write("WAVE", 4);
	stream->write("fmt ", 4);
	serialize(*stream, uint32_t(16)); //fmt header length
	serialize(*stream, uint16_t(1)); //PCM format
	serialize(*stream, uint16_t(channels)); //channels
	serialize(*stream, uint32_t(frequency)); //samplerate

	const auto framesize = channels * (bits + 7) / 8;
	serialize(*stream, uint32_t(frequency * framesize)); //bytes per second
	serialize(*stream, uint16_t(framesize)); //frame size
	serialize(*stream, uint16_t(bits)); //bits per sample

	stream->write("data", 4);
	serialize(*stream, uint32_t(0)); //offset 0x28: data size, will be fixed when closing file
}

wave_file::~wave_file() //seekp and write don't throw exceptions, so the destructor doesn't either
{
	stream->seekp(0x04); //fix file size
	serialize(*stream, uint32_t(total_length) + 44 - 8); //header size = 44

	stream->seekp(0x28); //fix data size
	serialize(*stream, uint32_t(total_length));
}

void wave_file::write_pcm(const void *src, size_t nbytes)
{
	stream->write((const char*)src, nbytes);
	total_length += nbytes;
}
