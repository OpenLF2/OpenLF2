#!/usr/bin/env bash
set -euo pipefail

lua_root=/workspace/build/web/vendor/lua-5.2.4
ffmpeg_root=/workspace/build/web/vendor/ffmpeg-7.1.5
prefix=/workspace/build/web/prefix
export EM_CACHE=/workspace/build/web/emscripten-cache

mkdir -p "$EM_CACHE/sysroot/lib/pkgconfig" "$prefix"
embuilder build sdl3 zlib bzip2
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
    -DOPENLF2_ICON_DIR=/workspace/build/web/icons \
    -DCMAKE_BUILD_TYPE=Release
cmake --build /workspace/build/web/native -j4
