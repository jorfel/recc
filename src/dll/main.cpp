#include <list>
#include <mutex>
#include <memory>
#include <fstream>
#include <iomanip>
#include <optional>
#include <stdexcept>

#include <io.h>
#include <fcntl.h>
#include <dsound.h>
#include <Windows.h>

#include "../win32_error.hpp"
#include "../handle_holder.hpp"

#include "pcm_file.hpp"
#include "wave_file.hpp"
#include "capture_dsound.hpp"

/// Global variables
std::mutex global_lock;
std::fstream global_log;
std::unique_ptr<std::fstream> global_out;
std::unique_ptr<outformat_base> global_format;
std::unique_ptr<capture_base> global_capture;


/// Sets log output file.
extern "C" __declspec(dllexport) HRESULT recc_log(const wchar_t *log)
{
	std::lock_guard _(global_lock);

	global_log = std::fstream();
	global_log.open(log, std::ios::out | std::ios::binary);
	if(!global_log)
		return errno;

	global_log << "Logging output from DLL." << std::endl;
	return 0;
}

/// Initialized or re-initializes capture to file specified by out.
extern "C" __declspec(dllexport) HRESULT recc_capture(const wchar_t *out, const char *api, const char *format)
{
	std::lock_guard _(global_lock);

	global_log << (global_capture ? "Initializing" : "Reinitializing") << " capture for API " << api << " with format " << format << " ..." << std::endl;

	if(global_capture)
	{
		global_capture.reset();
		global_format.reset();
		global_out.reset();
		global_log << "Old capture released." << std::endl;
	}

	try
	{
		auto file = std::make_unique<std::fstream>();
		file->open(out, std::ios::out | std::ios::binary);
		if(!*file)
			throw win32_error(errno, "Output file could not be opened.");

		//use local unique_ptr, so they will get deleted on error/exception
		std::unique_ptr<outformat_base> outformat;
		if(std::strcmp(format, "wav") == 0)
			outformat = std::make_unique<wave_file>(*file);
		else if(std::strcmp(format, "pcm") == 0)
			outformat = std::make_unique<pcm_file>(*file);
		else
			throw win32_error(0xFFF1, "Unknown output format \"" + std::string(api) + "\".");

		if(std::strcmp(api, "dsound") == 0)
			global_capture = std::make_unique<capture_dsound>(global_lock, global_log, *outformat);
		else
			throw win32_error(0xFFF2, "Unknown API \"" + std::string(api) + "\".");

		//commit values
		global_format = std::move(outformat);
		global_out = std::move(file);
		global_log << "Capture successfully initialized." << std::endl;
		return 0;
	}
	catch(const win32_error &e)
	{
		global_log << e.what() << " (code 0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << std::make_unsigned_t<decltype(e.code)>(e.code) << ")" << std::endl;
		return e.code;
	}
	catch(const std::exception &err)
	{
		global_log << "%s.\n" << err.what() << std::endl;
		return 0xFFF3;
	}
}

/// Releases capture.
extern "C" __declspec(dllexport) HRESULT recc_release()
{
	std::lock_guard _(global_lock);
	global_log << "Capture releasing ...\n";
	global_capture.reset();
	global_format.reset();
	global_out.reset();
	global_log << "Capture successfully released.\n";
	global_log.close();
	return 0;
}

/// Automatically release capture on DLL unload.
BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID)
{
	if(reason == DLL_PROCESS_DETACH)
		recc_release();
	
	return TRUE;
}