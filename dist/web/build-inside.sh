#!/usr/bin/env bash
set -euo pipefail

lua_root=/workspace/build/web/vendor/lua-5.2.4
ffmpeg_root=/workspace/build/web/vendor/ffmpeg-7.1.5
sdl_root=/workspace/build/web/vendor/SDL3-3.4.14
prefix=/workspace/build/web/prefix
export EM_CACHE=/workspace/build/web/emscripten-cache

mkdir -p "$EM_CACHE/sysroot/lib/pkgconfig" "$prefix"
embuilder build zlib bzip2
# SDL 3.4 instead of Emscripten's bundled 3.2 port: 3.2 paints black letterbox bars over each presented frame and
# stops resizing the canvas once the window is fullscreen.
if [[ ! -f "$prefix/lib/libSDL3.a" ]]; then
    emcmake cmake -S "$sdl_root" -B /workspace/build/web/sdl -G 'Unix Makefiles' \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF
    cmake --build /workspace/build/web/sdl -j4
    cmake --install /workspace/build/web/sdl
fi
make -B -C "$lua_root/src" -j4 liblua.a CC=emcc AR='emar rcu' RANLIB=emranlib MYCFLAGS=-O2

if [[ ! -f "$prefix/lib/libavformat.a" ]]; then
    (
        cd "$ffmpeg_root"
        emconfigure ./configure --prefix="$prefix" --target-os=none --arch=wasm32 \
            --enable-cross-compile --cc=emcc --cxx=em++ --ar=emar --ranlib=emranlib \
            --disable-asm --disable-autodetect --disable-network --disable-pthreads \
            --disable-doc --disable-programs --disable-avdevice --disable-avfilter \
            --disable-postproc --disable-everything --disable-shared --enable-static \
            --enable-decoder=wmav2 --enable-demuxer=asf --enable-swresample
        make -j4
        make install
    )
fi

emcmake cmake -S /workspace -B /workspace/build/web/native -G 'Unix Makefiles' \
    -DOPENLF2_DEPENDENCY_PROVIDER=web \
    -DOPENLF2_WEB_LUA_ROOT="$lua_root" \
    -DOPENLF2_WEB_FFMPEG_ROOT="$prefix" \
    -DSDL3_DIR="$prefix/lib/cmake/SDL3" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
    -DOPENLF2_ICON_DIR=/workspace/build/web/icons \
    -DCMAKE_BUILD_TYPE=Release
cmake --build /workspace/build/web/native -j4
