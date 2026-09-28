#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
luajit_source="$repo_root/build/vita/vendor/LuaJIT"

mkdir -p "$repo_root/build/vita/vendor"
if [[ ! -d "$luajit_source/.git" ]]; then
    git clone --quiet https://github.com/LuaJIT/LuaJIT.git "$luajit_source"
    git -C "$luajit_source" -c advice.detachedHead=false checkout --quiet --detach \
        c6ffc141a8762b41703f9287d63d93622a13dd8f
fi
[[ $(git -C "$luajit_source" rev-parse HEAD) == c6ffc141a8762b41703f9287d63d93622a13dd8f ]]
if ! git -C "$luajit_source" apply --reverse --check \
        "$repo_root/dist/vita/luajit-vita.patch" 2>/dev/null; then
    git -C "$luajit_source" apply --check "$repo_root/dist/vita/luajit-vita.patch"
    git -C "$luajit_source" apply "$repo_root/dist/vita/luajit-vita.patch"
fi

# The VPK's icon is rendered from the project's SVG here, on the host, and handed to the container.
python3 "$repo_root/tools/icons/make_icons.py" "$repo_root/build/vita/icons"

docker build --quiet --platform linux/amd64 -f "$repo_root/dist/vita/Dockerfile" \
    -t openlf2-vitasdk:local "$repo_root/dist/vita"
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -v "$repo_root:/workspace" \
    -w /workspace openlf2-vitasdk:local bash -ec '
    python3 dist/collect-licenses.py build/vita/licenses \
        --key ffmpeg-vita --key ffmpeg-vita-notes --key bzip2 --key openssl-vita \
        --key mbedtls-vita --key mpg123-vita \
        --local sdl3=/usr/local/vitasdk/arm-vita-eabi/share/licenses/SDL3/LICENSE.txt \
        --local luajit=build/vita/vendor/LuaJIT/COPYRIGHT \
        --local zlib=/usr/local/vitasdk/share/vdpm/licenses/zlib.txt \
        --local libcurl=/usr/local/vitasdk/share/vdpm/licenses/curl.txt \
        --local ca-certificates=/usr/share/doc/ca-certificates/copyright
    # The pinned VitaSDK image supplies this Mozilla-root trust bundle. Reject
    # an unexpected change so trust updates get an explicit review.
    printf "%s  %s\n" 9481fcd95f41b221f02f14d896535fe500bec539bc563c4cdca1acee483a8bdd \
        /etc/ssl/certs/ca-certificates.crt | sha256sum -c -
    cp /etc/ssl/certs/ca-certificates.crt build/vita/ca-certificates.crt
    make -C build/vita/vendor/LuaJIT/src -j4 libluajit.a \
        HOST_CC="gcc -m32" CROSS="$VITASDK/bin/arm-vita-eabi-" \
        TARGET_SYS=Other \
        XCFLAGS="-DLUAJIT_DISABLE_FFI -DLUAJIT_DISABLE_JIT -DLUAJIT_USE_SYSMALLOC" \
        TARGET_CFLAGS="-O2 -I$VITASDK/arm-vita-eabi/include"
    cmake -S . -B build/vita/native -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
        -DOPENLF2_DEPENDENCY_PROVIDER=vita \
        -DOPENLF2_VITA_LUAJIT_ROOT=/workspace/build/vita/vendor/LuaJIT \
        -DOPENLF2_LICENSE_DIR=/workspace/build/vita/licenses \
        -DOPENLF2_VITA_CA_BUNDLE=/workspace/build/vita/ca-certificates.crt \
        -DOPENLF2_ICON_DIR=/workspace/build/vita/icons \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build/vita/native -j4
    '

printf 'Vita build: %s\n' "$repo_root/build/vita/native/openlf2.vpk"
