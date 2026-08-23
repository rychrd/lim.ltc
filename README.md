# lim.ltc~

A Max/MSP external that generates SMPTE **LTC** (Linear Timecode) using [libltc](https://github.com/x42/libltc) for the
biphase-mark encoding and the [Max 8.2 SDK](https://github.com/Cycling74/max-sdk).


## Messages

- `time H M S F` — set the timecode origin (hours minutes seconds frames)
- `framerate F` — `24`, `25`, `29.97` (drop-frame), or `30`
- `start` or `1` — begin output at the set time
- `stop` or `0` — stop output (silence)

An optional creation argument sets the initial frame rate, e.g. `lim.ltc~ 25`.

## Layout

```
CMakeLists.txt            root project
source/lim.ltc~/          the external (lim.ltc~.c + CMakeLists.txt)
third_party/libltc/       libltc source (statically linked, LGPLv3)
max-sdk-base/             Max SDK base (headers, libs, cmake scripts)
externals/                build output: lim.ltc~.mxo / lim.ltc~.mxe64
```

## Build — macOS

```sh
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

The result is `externals/lim.ltc~.mxo`. For a universal (Intel + Apple Silicon)
build, uncomment `CMAKE_OSX_ARCHITECTURES` in the root `CMakeLists.txt`
(Apple Silicon also requires ad-hoc codesigning: `codesign --force --deep -s - externals/lim.ltc~.mxo`).

## Build — Windows

Use the MSVC toolchain (not MinGW):

```sh
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The result is `externals/lim.ltc~.mxe64`.

## Using it in Max

Place the built external in a Max package's `externals` folder. Then create a
`lim.ltc~` object.

## License
Licensed under the MIT License
libltc is LGPLv3 and is statically linked here; see `third_party/libltc/COPYING`.

