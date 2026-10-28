<p align="center"><img src="dist/io.github.openlf2.OpenLF2.svg" alt="OpenLF2 logo" width="128" height="128"></p>

# OpenLF2

OpenLF2 is a playable, open-source reimplementation of **Little Fighter 2 v2.0a**.
It recreates the original’s fast, side-view fighting gameplay and supports mods.

## What you can play

- Versus, Stage, Battle, Championship, Team Championship, and Demo modes
- Keyboard and controller play, with touch controls on supported screens
- Music, recordings, and replays (compatible)
- Network matches between OpenLF2 players

OpenLF2 is under active development. We continue to refine gameplay compatibility
with the original game.

## Beyond the original

- Keyboard, controller, and touch navigation on every menu — the original's launch
  screens are mouse-only
- An on-screen gamepad for touch devices
- An options page: upscaling filter (including an xBRZ-style shader), fullscreen toggle,
  FPS counter, controller/phone rumble, and an "unlock hidden characters" switch
- Optional CPU character shading and projected sprite shadows, with a bounded sprite cache
- A language option (English, Chinese, German) that draws the interface text with TrueType
  fonts, optionally antialiased; "default" keeps the original's bitmap font. Builds without
  SDL3_ttf offer only "default". See [Translations](#translations)

## Getting started

Try the [WebAssembly build in your browser](https://openlf2.github.io/OpenLF2/) — no install
needed beyond your own copy of the installer, see below.

### Downloads

| Platform | Downloads |
| --- | --- |
| Windows | [x86](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Windows-x86.exe) · [x64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Windows-x64.exe) · [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Windows-arm64.exe) |
| Linux | AppImage: [x86](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-i686.AppImage) · [x64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-x86_64.AppImage) · [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-aarch64.AppImage)<br>DEB: [x86](https://github.com/OpenLF2/OpenLF2/releases/latest/download/openlf2_i386.deb) · [x64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/openlf2_amd64.deb) · [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/openlf2_arm64.deb) |
| macOS | [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-macos-arm64.app.zip) · [x64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-macos-x64.app.zip) |
| Android | [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Android-api21-arm64-v8a.apk) · [ARMv7](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Android-api21-armeabi-v7a.apk) · [x86](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Android-api21-x86.apk) · [x64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Android-api21-x86_64.apk) |
| iOS | [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-iOS-arm64-unsigned.ipa) |
| Switch | [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Switch-arm64.nro) |
| PortMaster | [ARM64](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-PortMaster.zip) |
| Vita | [ARMv7](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Vita-armv7.vpk) |
| Web | [Browser build](https://github.com/OpenLF2/OpenLF2/releases/latest/download/OpenLF2-Web.zip) |

[<img src="https://flathub.org/api/badge?locale=en" alt="Get it on Flathub" width="160">](https://flathub.org/apps/io.github.openlf2.OpenLF2)

See the [full release page](https://github.com/OpenLF2/OpenLF2/releases) for checksums and release notes.

OpenLF2 needs the **Little Fighter 2 v2.0a installer** to load the original game data.
Both `LF2_v2.0a.exe` and `lf2_v20a_Setup.exe` are supported and verified by SHA-256.
The installer is not included in the downloads. Flatpak downloads it separately during
installation; on other platforms, provide your own copy when prompted. OpenLF2 reads
its resources without installing or extracting the game.

### Build from source

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

### Flatpak

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

### PS5 homebrew

The PS5 build is a homebrew title for jailbroken consoles. It uses the open-source
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
  -DOPENLF2_PS5_RUNTIME_MODULE=$NATIVE/runtime/libc.prx
cmake --build build/ps5
```

The title folder is written to `build/ps5/app/PPSA07091`; copy the whole folder to the console
and register it with a loader such as ShadowMount+. Copy `LF2_v2.0a.exe` or
`lf2_v20a_Setup.exe` to `/user/data/openlf2/` (the title's sandbox cannot list folders, so
there is no file picker). Startup steps appear as notifications and in
`/user/data/openlf2-startup.log`.

The PS5 build renders with SDL2's software renderer and has no translated text (SDL3_ttf is
unavailable) and no rumble; network games are untested. Choosing Quit leaves the title
idle; close it from the PS button menu.

## Network play

Network matches connect OpenLF2 players to other OpenLF2 players; compatibility with
original LF2 is not supported. Choose **Network Game** in the launch screen to host or join.
The default port is TCP 12345.

## Architecture

C++ owns engine mechanisms — state storage, rendering, audio, resource loading,
networking — behind small project-owned interfaces; platform libraries (SDL3, LuaJIT,
FFmpeg, libcurl) stay behind adapters. Lua owns gameplay and UI policy: combat rules, AI,
modes, menus, and HUD. The base game and mods share the same versioned script API.

## Modding

Mods are packages under `mods/`, declared with a `package.manifest` (id, version, required
API version, dependencies) that can add a module, extend a base module with a decorator,
replace it outright, or mount extra asset files. See
[`mods/example-character`](mods/example-character/README.md) for a working example, and
the [contribution guide](CONTRIBUTING.md) for how to propose engine changes.

## Translations

A language is a module `scripts/base/i18n/<id>.lua` returning a table of text to translation; `english`,
`chinese` and `german` are separate modules, and "default" (the original's bitmap font and bitmaps) has none.
Two kinds of text are translated:

- The scripts' own text: scripts pass English text to `i18n.tr` (`scripts/base/i18n/i18n.lua`).
- The text baked into the original's bitmaps: `scripts/base/i18n/baked.lua` lists where each string sits in
  each sheet (boxes, colors, alignment, no pixels). While a language is active, the sprite path paints the
  region over with its background color and draws the translation instead. The keys are the original's own
  strings, usually English with the Chinese in parentheses (`game start （開始遊戲）`): `english` keeps the
  English part, `chinese` the Chinese part, and "" erases a line.

A string without an entry stays as it is. To add a language, copy `german.lua`, list the module in
`scripts/base/package.manifest`, add its id to `i18n.languages`, and add the new language's name to every
existing language table. Text in a translated language is drawn by the host with `TextCommand` (SDL3_ttf;
fonts in `scripts/base/fonts/`, licenses beside them and in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)),
and each distinct text is rendered once and cached.

SDL3_ttf is optional at build time (`OPENLF2_TEXT_RENDERING`, on by default when it is found);
`-DOPENLF2_FETCH_SDL3_TTF=ON` builds it from its release archive (needs FreeType). The Web, Switch, PortMaster and
Vita builds include it.

## License

OpenLF2's code, scripts, and documentation are MIT-licensed ([LICENSE](LICENSE)). This
license does not cover the original game, its executable, or its assets.

## The original game

Little Fighter 2 was created by Marti Wong and Starsky Wong. Visit the
[original Little Fighter 2 website](https://www.lf2.net/en/intro.html). Support them with the
[Little Fighter 2 Remastered by Marti Wong](https://www.lf2.net/) or on its
[Steam page](https://store.steampowered.com/app/3249650/).

## Trademarks

Little Fighter 2, Nintendo Switch, PlayStation Vita, PlayStation 5, Windows, Android, macOS, and iOS
are names or marks of their respective rights holders. They are used here only to identify
the game or platforms. OpenLF2 is an independent project and is not affiliated with or
endorsed by those rights holders.
