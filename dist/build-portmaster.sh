#!/usr/bin/env bash
# Builds the PortMaster package (out/OpenLF2-PortMaster.zip) for aarch64. Run inside an arm64 Ubuntu 20.04
# container (glibc 2.31, so the binary starts on old handheld firmware):
#   docker run --rm --platform linux/arm64 -v "$PWD:/workspace" -w /workspace ubuntu:20.04 \
#       bash dist/build-portmaster.sh
# A newer GCC from the toolchain PPA compiles the C++23 code against the old glibc, and
# libstdc++ is linked statically so the device needs nothing newer than glibc 2.31.
# SDL3 is not Conan's: handhelds ship a patched SDL2 for their screen, audio and controller, so the
# game is linked to an SDL3 build whose backends call that SDL2 at run time.
set -euo pipefail

# arm64 is the real target; amd64 builds the same package layout so the toolchain and the
# dependency build can be tried on a PC (the result only runs on x86_64).
case "$(dpkg --print-architecture)" in
    arm64) conan_arch=armv8; device_arch=aarch64; march=-march=armv8-a ;;
    amd64) conan_arch=x86_64; device_arch=x86_64; march= ;;
    *) printf 'run this in an arm64 container\n' >&2; exit 1 ;;
esac

# SDL3 with the SDL2 backend (https://github.com/bmdhacks/SDL, branch sdl2-backend) and the
# SPIRV-Cross it needs for its GLES GPU backend; both pinned.
sdl_repository=https://github.com/bmdhacks/SDL.git
sdl_commit=6057d79baf8321bf190479a699655f06cc2a962f
spirv_repository=https://github.com/KhronosGroup/SPIRV-Cross.git
spirv_tag=vulkan-sdk-1.4.304.1
glibc_limit=2.31

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates curl git gnupg make perl pkg-config software-properties-common \
    binutils file zip unzip xz-utils
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y --no-install-recommends gcc-13 g++-13 python3-pip python3-venv
# Build scripts that look for plain gcc/g++/cc/c++ (LuaJIT's Makefile) get GCC 13 too.
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100
update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 100
update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 100
export CC=gcc-13 CXX=g++-13

root=$PWD
build_dir="$root/build/portmaster"
sdl_prefix="$build_dir/sdl-prefix"
conan_output="$build_dir/conan"
conan_archive="$build_dir/conan-cache.tgz"
package="$build_dir/package"
mkdir -p "$build_dir" "$conan_output" "$root/out"

# Focal's Python is too old for current Conan; uv fetches a standalone Python 3.11.
python3 -m pip install --disable-pip-version-check uv
uv venv --clear --python 3.11 "$build_dir/venv"
uv pip install --python "$build_dir/venv/bin/python" 'conan==2.32.0' 'cmake>=3.28,<4' ninja
export PATH="$build_dir/venv/bin:$PATH"

# --- SDL3 over the device's SDL2 ---
rm -rf "$build_dir/sdl-src" "$build_dir/spirv-cross" "$build_dir/sdl-build"
git clone --quiet "$sdl_repository" "$build_dir/sdl-src"
git -C "$build_dir/sdl-src" checkout --quiet "$sdl_commit"
git clone --quiet --depth 1 --branch "$spirv_tag" "$spirv_repository" "$build_dir/spirv-cross"
cmake -S "$build_dir/sdl-src" -B "$build_dir/sdl-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sdl_prefix" \
    "-DCMAKE_C_FLAGS=$march" "-DCMAKE_CXX_FLAGS=$march" \
    -DSDL_SDL2_BACKEND=ON -DSDL_SPIRV_CROSS_DIR="$build_dir/spirv-cross" \
    -DSDL_DUMMYVIDEO=OFF -DSDL_OFFSCREEN=OFF -DSDL_DUMMYAUDIO=OFF \
    -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build "$build_dir/sdl-build" --parallel "$(nproc)"
cmake --install "$build_dir/sdl-build"

# --- the other dependencies, from Conan, built against the container's glibc ---
export CONAN_HOME="$build_dir/conan-home"
conan profile detect --force
if [[ -f "$conan_archive" ]]; then
    conan cache restore "$conan_archive"
fi
conan export conan/luajit
cmake_version=$(cmake --version | sed -n '1s/^cmake version \([0-9][0-9.]*\)$/\1/p')
profile="$build_dir/conan-ci.profile"
cat > "$profile" <<PROFILE
[settings]
os=Linux
arch=$conan_arch
compiler=gcc
compiler.version=13
compiler.libcxx=libstdc++11
compiler.cppstd=23
build_type=Release

[replace_tool_requires]
cmake/*: cmake/$cmake_version

[platform_tool_requires]
cmake/$cmake_version

[conf]
user.openlf2:external_sdl=True
tools.build:compiler_executables={"c": "gcc-13", "cpp": "g++-13"}
tools.system.package_manager:mode=install
PROFILE
# ConanCenter's prebuilt build tools (m4, pkgconf, ...) need a newer glibc than this container's, so
# they are compiled here; everything else missing is built as usual.
conan install . --output-folder="$conan_output" -pr:h "$profile" -pr:b "$profile" --build=missing \
    --build='m4/*' --build='pkgconf/*' --build='automake/*' --build='autoconf/*' --build='libtool/*' \
    --build='ninja/*' --build='nasm/*'
conan cache save "*:*" --no-source --file="$build_dir/conan-cache-next.tgz"
mv "$build_dir/conan-cache-next.tgz" "$conan_archive"

# --- the game ---
cmake -S . -B "$build_dir/game" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_TOOLCHAIN_FILE="$conan_output/conan_toolchain.cmake" \
    -DOPENLF2_DEPENDENCY_PROVIDER=conan \
    -DSDL3_DIR="$sdl_prefix/lib/cmake/SDL3" \
    -DCMAKE_EXE_LINKER_FLAGS='-static-libstdc++ -static-libgcc'
cmake --build "$build_dir/game" --parallel "$(nproc)"
DESTDIR="$build_dir/install" cmake --install "$build_dir/game"
binary="$build_dir/install/usr/bin/openlf2"
test -x "$binary"
test -f "$build_dir/install/usr/bin/scripts/base/package.manifest"

# --- the package: OpenLF2.sh next to openlf2/ ---
rm -rf "$package"
install -d "$package/openlf2/libs.$device_arch" "$package/openlf2/licenses" "$package/openlf2/conf"
install -m 0755 dist/portmaster/OpenLF2.sh "$package/OpenLF2.sh"
install -m 0644 dist/portmaster/port.json dist/portmaster/gameinfo.xml "$package/"
# PortMaster wants a README.md next to them for the submission; it is optional here.
if [[ -f dist/portmaster/README.md ]]; then install -m 0644 dist/portmaster/README.md "$package/"; fi
install -m 0755 "$binary" "$package/openlf2/openlf2.$device_arch"
# conf/ is where the launcher keeps config.json and where LF2_v2.0a.exe goes.
install -m 0644 dist/portmaster/conf-README.txt "$package/openlf2/conf/README.txt"
cp -a "$build_dir/install/usr/bin/scripts" "$package/openlf2/scripts"

# Bundle what the binary links from outside the system: the SDL3 shim and Conan's shared libraries.
# SDL2 itself is the device's.
source "$conan_output/conanrun.sh" 2>/dev/null || true
export LD_LIBRARY_PATH="$sdl_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ldd "$binary" | awk '/=> \//{print $3}' | while read -r library; do
    case "$library" in
        "$sdl_prefix"/*|"$CONAN_HOME"/*|"$build_dir"/*) cp -L "$library" "$package/openlf2/libs.$device_arch/" ;;
    esac
done
for library in SDL3 avformat avcodec avutil swresample; do
    if ! ls "$package/openlf2/libs.$device_arch/" | grep -q "lib${library}\.so"; then
        printf 'lib%s was not bundled\n' "$library" >&2
        exit 1
    fi
done
strip --strip-unneeded "$package/openlf2/openlf2.$device_arch" "$package"/openlf2/libs.$device_arch/*.so*

# Nothing may need a newer glibc than the oldest firmware's.
newest=$(objdump -T "$package/openlf2/openlf2.$device_arch" "$package"/openlf2/libs.$device_arch/*.so* 2>/dev/null |
    grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n 1)
printf 'newest glibc symbol needed: %s\n' "$newest"
if [[ $(printf '%s\n%s\n' "$newest" "$glibc_limit" | sort -V | tail -n 1) != "$glibc_limit" ]]; then
    printf 'needs glibc %s, newer than %s\n' "$newest" "$glibc_limit" >&2
    exit 1
fi
(cd "$package/openlf2" && LD_LIBRARY_PATH="$PWD/libs.$device_arch" ./openlf2.$device_arch --help > /dev/null)

install -m 0644 "$build_dir/sdl-src/LICENSE.txt" "$package/openlf2/licenses/SDL3-LICENSE.txt"
install -m 0644 "$build_dir/spirv-cross/LICENSE" "$package/openlf2/licenses/SPIRV-Cross-LICENSE.txt"
python3 dist/collect-licenses.py "$package/openlf2/licenses" \
    --key ffmpeg --key ffmpeg-notes --key luajit --key zlib --key bzip2 --key openssl --key libcurl
cat > "$package/openlf2/licenses/SDL3-shim.txt" <<NOTE
libs.$device_arch/libSDL3.so.0 is SDL3 built with the SDL2 backend from
$sdl_repository at commit $sdl_commit, with SPIRV-Cross $spirv_tag.
It loads the device's own libSDL2-2.0.so.0 at run time.
NOTE

output="$root/out/OpenLF2-PortMaster.zip"
rm -f "$output"
(cd "$package" && zip -qr "$output" .)
(cd "$root/out" && sha256sum OpenLF2-PortMaster.zip > OpenLF2-PortMaster.zip.sha256)
