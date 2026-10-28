# Building OpenLF2

## Build from source

Requirements: CMake 3.25+, Ninja, a C++23 compiler, and development packages for SDL3,
LuaJIT, zlib, bzip2, OpenSSL, FFmpeg, and libcurl. SDL3_ttf is optional (translated languages).

```sh
cmake --preset debug
cmake --build --preset debug
build/debug/openlf2 --installer path/to/LF2_v2.0a.exe
```

Useful flags: `--config-dir DIR` (settings/data location), `--mod PATH` (load a mod),
`--headless` (no window), `--scripts DIR` (run from source scripts instead of the copy
beside the executable).

## Flatpak

Build with the Freedesktop 26.08 SDK (`flatpak`, `flatpak-builder`, and `appstream` required):

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
bash dist/build-flatpak.sh
flatpak remote-add --user --if-not-exists --no-gpg-verify openlf2-local build/flatpak-repo
flatpak install --user openlf2-local io.github.openlf2.OpenLF2
flatpak run io.github.openlf2.OpenLF2
```

The installer downloads automatically from archive.org during installation.
The repository archive is written to `out/OpenLF2-flatpak-repo.tar.gz`.
Controller access requires Flatpak 1.15.6+.

## PS5 homebrew

The PS5 build is a homebrew title. It uses the open-source
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) toolchain, PacBrew's SDL2, FFmpeg,
LuaJIT and curl ports, and the ELF converter and `libc.prx` runtime shim from
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
Build the boilerplate once on Linux or WSL (`make`, plus `make pacbrew` for the ports), then
configure OpenLF2 with its paths:

```sh
NATIVE=/path/to/ps5-native-app-boilerplate
cmake -S . -B build/ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$NATIVE/.deps/native/ps5-payload-sdk/toolchain/prospero.cmake \
  -DOPENLF2_PS5_PACBREW_ROOT=$NATIVE/.deps/pacbrew/v0.40.2/sysroot \
  -DOPENLF2_PS5_NATIVE_TOOL=$NATIVE/build/host/ps5-native-tool \
  -DOPENLF2_PS5_RUNTIME_MODULE=$NATIVE/runtime/libc.prx \
  -DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config -DPKG_CONFIG_ARGN=--static
cmake --build build/ps5
```

The last two options make CMake use the system `pkg-config` instead of the SDK's `prospero-pkg-config` wrapper, which overrides the PacBrew search path. If the linker `ld.lld` is not found, install `lld` for your LLVM version.

The CI workflow `.github/workflows/ps5.yml` runs the same steps through `dist/build-ps5.sh` and publishes the title folder as `OpenLF2-PS5-x64.zip`.

The title folder is written to `build/ps5/app/PPSA07091`; copy the whole folder to the console
and register it with a loader such as ShadowMount+. Copy `LF2_v2.0a.exe` or
`lf2_v20a_Setup.exe` to `/user/data/openlf2/` (the title's sandbox cannot list folders, so
there is no file picker). Startup steps appear as notifications and in
`/user/data/openlf2-startup.log`.

The PS5 build renders with SDL2's software renderer and has no translated text (SDL3_ttf is
unavailable) and no rumble; network games are untested. Choosing Quit leaves the title
idle; close it from the PS button menu.
