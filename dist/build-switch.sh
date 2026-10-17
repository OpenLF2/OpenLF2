#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
vendor_dir="$repo_root/build/switch/vendor"
sdl_source="$vendor_dir/SDL"
sdl_patch="$vendor_dir/sdl3-switch"
luajit_source="$vendor_dir/LuaJIT"
freetype_source="$vendor_dir/freetype-2.13.2"
sdl_ttf_source="$vendor_dir/SDL3_ttf-3.2.2"
image='devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282'

mkdir -p "$vendor_dir"
if [[ ! -d "$sdl_source/.git" ]]; then
    git clone --quiet --branch release-3.4.14 --depth 1 https://github.com/libsdl-org/SDL.git "$sdl_source"
fi
if [[ ! -d "$sdl_patch/.git" ]]; then
    git clone --quiet https://github.com/neomody77/sdl3-switch.git "$sdl_patch"
    git -C "$sdl_patch" -c advice.detachedHead=false checkout --quiet --detach 182e511214d7600e4bdab8606d7caf0ef744afd6
fi
if [[ ! -d "$luajit_source/.git" ]]; then
    git clone --quiet https://github.com/LuaJIT/LuaJIT.git "$luajit_source"
    git -C "$luajit_source" checkout --quiet --detach c6ffc141a8762b41703f9287d63d93622a13dd8f
fi

# SDL3_ttf and FreeType (the translated languages' text), from their release archives.
fetch_source() {
    local name=$1 url=$2 digest=$3 directory=$4
    if [[ ! -f "$vendor_dir/$name" ]]; then
        curl --fail --location --retry 3 --output "$vendor_dir/$name" "$url"
    fi
    printf '%s  %s\n' "$digest" "$vendor_dir/$name" | sha256sum --check
    if [[ ! -d "$vendor_dir/$directory" ]]; then tar -xf "$vendor_dir/$name" -C "$vendor_dir"; fi
}
fetch_source freetype-2.13.2.tar.xz https://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.xz \
    12991c4e55c506dd7f9b765933e62fd2be2e06d421505d7950a132e4f1bb484d freetype-2.13.2
fetch_source SDL3_ttf-3.2.2.tar.gz https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz \
    63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d SDL3_ttf-3.2.2

[[ $(git -C "$sdl_source" rev-parse HEAD) == 147a8ee32dbf9ac02f3794964490687b6bbda1bc ]]
[[ $(git -C "$sdl_patch" rev-parse HEAD) == 182e511214d7600e4bdab8606d7caf0ef744afd6 ]]
[[ $(git -C "$luajit_source" rev-parse HEAD) == c6ffc141a8762b41703f9287d63d93622a13dd8f ]]

if ! git -C "$sdl_source" apply --reverse --check -p3 "$sdl_patch/sdl3-switch.patch" 2>/dev/null; then
    git -C "$sdl_source" apply --check -p3 "$sdl_patch/sdl3-switch.patch"
    git -C "$sdl_source" apply -p3 "$sdl_patch/sdl3-switch.patch"
fi
if ! git -C "$luajit_source" apply --reverse --check "$repo_root/dist/switch/luajit-switch.patch" 2>/dev/null; then
    git -C "$luajit_source" apply --check "$repo_root/dist/switch/luajit-switch.patch"
    git -C "$luajit_source" apply "$repo_root/dist/switch/luajit-switch.patch"
fi

# The NRO's icon is rendered from the project's SVG here, on the host, and handed to the container.
python3 "$repo_root/tools/icons/make_icons.py" "$repo_root/build/switch/icons"

docker run --rm --user "$(id -u):$(id -g)" -v "$repo_root:/work/openlf2" \
    -w /work/openlf2 "$image" sh -ec '
    python3 dist/collect-licenses.py build/switch/licenses \
        --key ffmpeg-switch --key ffmpeg-switch-notes --key libcurl-switch --key egl-switch \
        --local sdl3=build/switch/vendor/SDL/LICENSE.txt \
        --local sdl3-ttf=build/switch/vendor/SDL3_ttf-3.2.2/LICENSE.txt \
        --local freetype=build/switch/vendor/freetype-2.13.2/docs/FTL.TXT \
        --local luajit=build/switch/vendor/LuaJIT/COPYRIGHT \
        --local zlib=/opt/devkitpro/portlibs/switch/licenses/switch-zlib/LICENSE \
        --local bzip2=/opt/devkitpro/portlibs/switch/licenses/switch-bzip2/LICENSE \
        --local mbedtls=/opt/devkitpro/portlibs/switch/licenses/switch-mbedtls/LICENSE
    cmake -S build/switch/vendor/SDL -B build/switch/vendor/SDL/build-switch -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_INSTALL_PREFIX=/work/openlf2/build/switch/vendor/SDL/build-switch/stage \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build build/switch/vendor/SDL/build-switch -j4
    cmake --install build/switch/vendor/SDL/build-switch
    stage=/work/openlf2/build/switch/vendor/SDL/build-switch/stage
    cmake -S build/switch/vendor/freetype-2.13.2 -B build/switch/vendor/freetype-2.13.2/build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_INSTALL_PREFIX="$stage" \
        -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    cmake --build build/switch/vendor/freetype-2.13.2/build -j4
    cmake --install build/switch/vendor/freetype-2.13.2/build
    cmake -S build/switch/vendor/SDL3_ttf-3.2.2 -B build/switch/vendor/SDL3_ttf-3.2.2/build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_INSTALL_PREFIX="$stage" \
        -DCMAKE_PREFIX_PATH="$stage" -DBUILD_SHARED_LIBS=OFF -DSDLTTF_VENDORED=OFF -DSDLTTF_HARFBUZZ=OFF \
        -DSDLTTF_PLUTOSVG=OFF -DSDLTTF_SAMPLES=OFF
    cmake --build build/switch/vendor/SDL3_ttf-3.2.2/build -j4
    cmake --install build/switch/vendor/SDL3_ttf-3.2.2/build
    make -C build/switch/vendor/LuaJIT/src -j4 libluajit.a \
        HOST_CC=gcc CROSS=/opt/devkitpro/devkitA64/bin/aarch64-none-elf- \
        TARGET_SYS=Other XCFLAGS="-DLUAJIT_USE_SYSMALLOC -DLUAJIT_DISABLE_JIT" \
        TARGET_CFLAGS="-O2 -fPIC -D__SWITCH__ -I/opt/devkitpro/libnx/include -I/opt/devkitpro/portlibs/switch/include"
    cmake -S . -B build/switch/native -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_PREFIX_PATH=/work/openlf2/build/switch/vendor/SDL/build-switch/stage \
        -DOPENLF2_SWITCH_LUAJIT_ROOT=/work/openlf2/build/switch/vendor/LuaJIT \
        -DOPENLF2_LICENSE_DIR=/work/openlf2/build/switch/licenses \
        -DOPENLF2_ICON_DIR=/work/openlf2/build/switch/icons
    cmake --build build/switch/native -j4
    '

printf 'Switch build: %s\n' "$repo_root/build/switch/native/openlf2.nro"
