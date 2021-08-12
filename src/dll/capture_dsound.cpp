#include "capture_dsound.hpp"

#include "../handle_holder.hpp"
#include "../win32_error.hpp"


static constexpr auto com_releaser = [](IUnknown *t) { t->Release(); };
template<class T>
using com_ptr = std::unique_ptr<T, decltype(com_releaser)>;


capture_dsound::capture_dsound(std::mutex &lock, std::ostream &log, outformat_base &out) : lock(&lock), log(&log), output(&out), output_setup(false)
{
	//create dummy IDirectSound and IDirectSoundBuffer
	HMODULE hDirectSound = GetModuleHandleA("dsound.dll");
	if(!hDirectSound)
		throw win32_error(GetLastError(), "dsound.dll not loaded.");

	using DirectSoundCreate_sig = HRESULT WINAPI(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);
	DirectSoundCreate_sig *dsc = (DirectSoundCreate_sig *)GetProcAddress(hDirectSound, "DirectSoundCreate");
	if(!dsc)
		throw win32_error(GetLastError(), "DirectSoundCreate not in dsound.dll.");

	IDirectSound *sounddev;
	if(auto err = dsc(nullptr, &sounddev, nullptr); err != S_OK)
		throw win32_error(err, "DirectSoundCreate failed.");

	com_ptr<IDirectSound> sounddev_raii(sounddev, com_releaser);

	WAVEFORMATEX wfmt = { 0 };
	wfmt.wFormatTag = 1;
	wfmt.nChannels = 2;
	wfmt.nSamplesPerSec = 44100;
	wfmt.wBitsPerSample = 16;
	wfmt.nBlockAlign = wfmt.wBitsPerSample * wfmt.nChannels / 8;
	wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;
	wfmt.cbSize = 12;

	DSBUFFERDESC buffdesc = { sizeof(DSBUFFERDESC), 0, DSBSIZE_MIN, 0, &wfmt, GUID_NULL };
	IDirectSoundBuffer *soundbuff;
	if(auto err = sounddev->CreateSoundBuffer(&buffdesc, &soundbuff, nullptr); err != S_OK)
		throw win32_error(err, "CreateSoundBuffer failed.");

	com_ptr<IDirectSoundBuffer> soundbuff_raii(soundbuff, com_releaser);

	vtable = *(unlock_func***)soundbuff;
	old_query = vtable[0];
	old_unlock = vtable[19];

	DWORD o;
	if(!VirtualProtect(vtable, 0x1000, PAGE_READWRITE, &o))
		throw win32_error(GetLastError(), "VirtualProtect failed.");

	vtable[0] = (unlock_func*)this; //first entry is QueryInterface, but probably unused, so put this in there
	vtable[19] = &hook_unlock; //if aligned, this is atomic on x86
}

capture_dsound::~capture_dsound()
{
	vtable[19] = old_unlock;
	vtable[0] = old_query;
}

HRESULT WINAPI capture_dsound::hook_unlock(IDirectSoundBuffer *buff, LPVOID ptr1, DWORD len1, LPVOID ptr2, DWORD len2)
{
	unlock_func **vtable = *(unlock_func***)buff;
	capture_dsound *that = (capture_dsound*)vtable[0];
	
	std::lock_guard _(*that->lock);

	auto res = that->old_unlock(buff, ptr1, len1, ptr2, len2);
	if(res == S_OK)
	{
		if(!that->output_setup)
		{
			WAVEFORMATEX format;
			buff->GetFormat(&format, sizeof(format), nullptr);

			(*that->log) << "Output format: " << format.nSamplesPerSec << "Hz, " << format.wBitsPerSample << " bits, " << format.nChannels << " channels." << std::endl;
			that->output->setup(format.nSamplesPerSec, format.wBitsPerSample, format.nChannels);
			that->output_setup = true;
		}

		that->output->write_pcm(ptr1, size_t(len1));

		if(ptr2)
			that->output->write_pcm(ptr2, size_t(len2));
	}

	return res;
}
