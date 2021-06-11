# recc - Audio capturing program 

Captures audio directly from Windows-x64 applications using DLL injection.

## Usage

    Records audio output from another application.
    Usage:
      recc [OPTION...]

      -p, --pid number    Process ID of target application.
      -w, --window text   Window title or part of it of a target application.
                          Overwrites -p.
      -a, --api text      The audio API to use. (default: dsound)
      -o, --output path   Output wave file. Use -- for stdout. (default:
                          ./rec.wav)
      -l, --log path      Output log file. Use -- for stderr. (default: --)
      -f, --format text   Output format. (default: wav)

    Supported audio APIs:
      dsound (DirectSound)
    Supported audio formats:
      wav (RIFF WAVE)
      pcm (raw PCM)
    Sampling information (frequency, bit depth, channels) depend on the output of the target application.

## Building

Run [cmake](https://cmake.org/) and build with latest MSVC. It must support C++20 and coroutines.

This will build a standalone version:
1) Create a folder and switch to it
2) Run `cmake -G "Visual Studio 16 2019" -A x64 -D ASMJIT_STATIC=ON ..`
3) Run `cmake --build . --config release`

Now check out the `Release` folder.

## Used libraries

* [cxxopts](https://github.com/jarro2783/cxxopts)
* [asmjit](https://asmjit.com/)