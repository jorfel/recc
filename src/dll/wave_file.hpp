#pragma once

#include <iosfwd>
#include <cstddef>

#include "capture_base.hpp"

///
/// Simple .wav-file-writer
/// The output stream is required to be seekable because the header of the file
/// has to be fixed when the file is closed and the total file size is known.
///
class wave_file : public outformat_base
{
public:
	wave_file(wave_file&&) = delete;

	wave_file(std::ostream &o);
	~wave_file();

	void setup(unsigned frequency, unsigned bits, unsigned channels) override;
	void write_pcm(const void *src, size_t nbytes) override;

private:
	std::streamsize total_length;
	std::ostream *stream = nullptr;
};