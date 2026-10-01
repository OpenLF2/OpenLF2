#!/usr/bin/env bash
# Run inside the architecture-matched Debian 13 container used by the CI matrix.
set -euo pipefail

case "${1:-}" in
    x86)
        debian_arch=i386
        conan_arch=x86
        appimage_arch=i686
        deploy_asset=linuxdeploy-i386.AppImage
        deploy_sha=a2a88d142aac42db779483ca07c10dbf318b27f514691107fc88a202faae17b5
        tool_asset=appimagetool-i686.AppImage
        tool_sha=7ad9ff47c203aae0149b18f6df9e3018b2e2f470ea644a0413e3ded39e9e3bdb
        runtime_sha=e72ea0b140a0a16e680713238a6f30aad278b62c4ca17919c554864124515498
        ;;
    x64)
        debian_arch=amd64
        conan_arch=x86_64
        appimage_arch=x86_64
        deploy_asset=linuxdeploy-x86_64.AppImage
        deploy_sha=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
        tool_asset=appimagetool-x86_64.AppImage
        tool_sha=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
        runtime_sha=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
        ;;
    arm64)
        debian_arch=arm64
        conan_arch=armv8
        appimage_arch=aarch64
        deploy_asset=linuxdeploy-aarch64.AppImage
        deploy_sha=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
        tool_asset=appimagetool-aarch64.AppImage
        tool_sha=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158
        runtime_sha=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444
        ;;
    *)
        printf 'usage: %s x86|x64|arm64\n' "$0" >&2
        exit 2
        ;;
esac

if [[ $(dpkg --print-architecture) != "$debian_arch" ]]; then
    printf 'expected Debian architecture %s\n' "$debian_arch" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    appstream binutils ca-certificates cmake curl desktop-file-utils file g++ git \
    libasound2-data libasound2-dev libegl-dev libgl-dev libwayland-dev libx11-dev libxcursor-dev \
    libxext-dev libxfixes-dev libxi-dev libxkbcommon-dev libxrandr-dev libxss-dev \
    make ninja-build pkg-config python3 python3-venv squashfs-tools xz-utils

desktop-file-validate dist/io.github.openlf2.OpenLF2.desktop
appstreamcli validate --no-net dist/io.github.openlf2.OpenLF2.metainfo.xml

build_dir="$PWD/build/appimage-${debian_arch}"
appdir="$build_dir/AppDir"
tools_dir="$build_dir/tools"
conan_output="$build_dir/conan"
conan_lock="$build_dir/conan.lock"
conan_profile="$build_dir/conan-ci.profile"
conan_archive="$build_dir/conan-cache.tgz"
mkdir -p "$tools_dir" "$conan_output" out

# Dependency resolution and build use Conan 2. The lock records exact recipe revisions
# chosen for this architecture and is shipped with the AppImage.
python3 -m venv "$build_dir/conan-venv"
conan="$build_dir/conan-venv/bin/conan"
"$build_dir/conan-venv/bin/pip" install --disable-pip-version-check 'conan==2.32.0'
export CONAN_HOME="$build_dir/conan-home"
"$conan" profile detect --force
if [[ -f "$conan_archive" ]]; then
    "$conan" cache restore "$conan_archive"
fi
"$conan" export conan/luajit

# ConanCenter can request a CMake tool package with no x86 binary; use the container's own CMake instead.
system_cmake_version=$(cmake --version | sed -n '1s/^cmake version \([0-9][0-9.]*\)$/\1/p')
if [[ -z "$system_cmake_version" ]]; then
    printf 'unable to determine system CMake version\n' >&2
    exit 1
fi
cat > "$conan_profile" <<EOF
include(default)

[replace_tool_requires]
cmake/*: cmake/$system_cmake_version

[platform_tool_requires]
cmake/$system_cmake_version

[conf]
tools.system.package_manager:mode=install
EOF
"$conan" lock create . --lockfile="" --lockfile-out="$conan_lock" \
    -pr:h "$conan_profile" -pr:b "$conan_profile" \
    -s:h "arch=$conan_arch" -s:b "arch=$conan_arch" \
    -s:h build_type=Release -s:b build_type=Release -s:h compiler.cppstd=23
"$conan" install . --lockfile="$conan_lock" --output-folder="$conan_output" \
    -pr:h "$conan_profile" -pr:b "$conan_profile" \
    --build=missing -s:h "arch=$conan_arch" -s:b "arch=$conan_arch" \
    -s:h build_type=Release -s:b build_type=Release -s:h compiler.cppstd=23
# Save compiled binaries and recipe metadata for the next CI run. Conan omits
# temporary build/download folders; --no-source also omits extracted sources.
"$conan" cache save "*:*" --no-source --file="$build_dir/conan-cache-next.tgz"
mv "$build_dir/conan-cache-next.tgz" "$conan_archive"
source "$conan_output/conanrun.sh"

# Include the exact upstream FFmpeg source and license for the shared Conan package.
ffmpeg_version=7.1.5
ffmpeg_sha=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
ffmpeg_archive="$tools_dir/ffmpeg-$ffmpeg_version.tar.xz"
curl --fail --location --retry 3 --output "$ffmpeg_archive" \
    "https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz"
printf '%s  %s\n' "$ffmpeg_sha" "$ffmpeg_archive" | sha256sum --check
tar -xf "$ffmpeg_archive" -C "$build_dir" "ffmpeg-$ffmpeg_version/COPYING.LGPLv2.1"

# Catch an accidental GPL build or a minimal build missing either required component.
mkdir -p "$build_dir/ffmpeg-check"
cat > "$build_dir/ffmpeg-check/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.25)
project(OpenLF2FfmpegCheck LANGUAGES C)
find_package(FFmpeg CONFIG REQUIRED)
add_executable(check_ffmpeg check_ffmpeg.c)
target_link_libraries(check_ffmpeg PRIVATE FFmpeg::avformat FFmpeg::avcodec
    FFmpeg::avutil FFmpeg::swresample)
EOF
cat > "$build_dir/ffmpeg-check/check_ffmpeg.c" <<'EOF'
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <string.h>
static int check_license(const char *library, const char *license) {
    printf("%s license: %s\n", library, license);
    if (strncmp(license, "LGPL version", 12) == 0) {
        return 0;
    }
    fprintf(stderr, "%s must be built under the LGPL\n", library);
    return 1;
}
int main(void) {
    int failed = 0;
    failed |= check_license("libavcodec", avcodec_license());
    failed |= check_license("libavformat", avformat_license());
    failed |= check_license("libavutil", avutil_license());
    failed |= check_license("libswresample", swresample_license());
    if (avcodec_find_decoder_by_name("wmav2") == NULL) {
        fprintf(stderr, "FFmpeg is missing the wmav2 decoder\n");
        failed = 1;
    }
    if (av_find_input_format("asf") == NULL) {
        fprintf(stderr, "FFmpeg is missing the asf demuxer\n");
        failed = 1;
    }
    return failed;
}
EOF
cmake -S "$build_dir/ffmpeg-check" -B "$build_dir/ffmpeg-check/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$conan_output/conan_toolchain.cmake"
cmake --build "$build_dir/ffmpeg-check/build" --parallel "$(nproc)"
"$build_dir/ffmpeg-check/build/check_ffmpeg"

cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_TOOLCHAIN_FILE="$conan_output/conan_toolchain.cmake" \
    -DOPENLF2_DEPENDENCY_PROVIDER=conan
cmake --build "$build_dir" --parallel "$(nproc)"
DESTDIR="$appdir" cmake --install "$build_dir"
install -d "$appdir/usr/share/doc/openlf2"
install -m 0644 "$ffmpeg_archive" "$appdir/usr/share/doc/openlf2/"
install -m 0644 "$build_dir/ffmpeg-$ffmpeg_version/COPYING.LGPLv2.1" \
    "$appdir/usr/share/doc/openlf2/FFmpeg-LGPL-2.1.txt"
install -m 0644 dist/build-appimage.sh "$appdir/usr/share/doc/openlf2/build-appimage.sh"
install -m 0644 conanfile.py "$appdir/usr/share/doc/openlf2/conanfile.py"
install -m 0644 "$conan_lock" "$appdir/usr/share/doc/openlf2/conan.lock"
install -m 0644 "$conan_profile" "$appdir/usr/share/doc/openlf2/conan-ci.profile"
python3 dist/collect-licenses.py "$appdir/usr/share/doc/openlf2/licenses" \
    --key ffmpeg --key ffmpeg-notes --key sdl3 --key luajit --key zlib --key bzip2 \
    --key openssl --key libcurl --key appimage-runtime
cat > "$appdir/usr/share/doc/openlf2/FFmpeg-build.txt" <<EOF
FFmpeg $ffmpeg_version source: ffmpeg-$ffmpeg_version.tar.xz
SHA-256: $ffmpeg_sha
Upstream: https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz
Build recipe: conanfile.py, conan-ci.profile and build-appimage.sh, in this directory.
Conan package recipe and transitive revisions: conan.lock, in this directory.
The archive is the exact upstream source; ConanCenter supplies the build recipe,
which may adjust build files. The lockfile identifies its recipe revision.
License: LGPL 2.1 or later; see FFmpeg-LGPL-2.1.txt.
Shared libraries are in the AppImage's usr/lib directory and can be replaced after extraction.
EOF

# These releases are fixed inputs. Check the published SHA-256 before executing them.
curl --fail --location --retry 3 --output "$tools_dir/$deploy_asset" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/$deploy_asset"
curl --fail --location --retry 3 --output "$tools_dir/$tool_asset" \
    "https://github.com/AppImage/appimagetool/releases/download/1.9.1/$tool_asset"
runtime_file="$tools_dir/runtime-$appimage_arch"
curl --fail --location --retry 3 --output "$runtime_file" \
    "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-$appimage_arch"
printf '%s  %s\n' "$deploy_sha" "$tools_dir/$deploy_asset" | sha256sum --check
printf '%s  %s\n' "$tool_sha" "$tools_dir/$tool_asset" | sha256sum --check
printf '%s  %s\n' "$runtime_sha" "$runtime_file" | sha256sum --check
chmod +x "$tools_dir/$deploy_asset" "$tools_dir/$tool_asset"

APPIMAGE_EXTRACT_AND_RUN=1 "$tools_dir/$deploy_asset" --appdir "$appdir" \
    --executable "$appdir/usr/bin/openlf2" \
    --desktop-file "$appdir/usr/share/applications/io.github.openlf2.OpenLF2.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/io.github.openlf2.OpenLF2.png"
# Keep host PulseAudio out of the AppImage.
if find "$appdir/usr" -name 'libpulse*.so*' -print -quit | grep -q .; then
    printf 'PulseAudio library bundled unexpectedly; review its notice and dependencies\n' >&2
    exit 1
fi
if find "$appdir/usr" -type f -name 'libSDL3.so*' -exec readelf -d {} + | \
        grep -Eq '\(NEEDED\).*libpulse'; then
    printf 'SDL3 links directly to PulseAudio instead of loading the host library\n' >&2
    exit 1
fi
# libasound needs its data files alongside its .so, not just the library itself.
test -f /usr/share/alsa/alsa.conf
install -d "$appdir/usr/share"
cp -a /usr/share/alsa "$appdir/usr/share/"
install -m 0644 /usr/share/doc/libasound2-data/copyright \
    "$appdir/usr/share/doc/openlf2/ALSA-data-copyright"
mv "$appdir/AppRun" "$appdir/AppRun.linuxdeploy"
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
APPDIR="${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}"
export APPDIR
ALSA_CONFIG_DIR="${ALSA_CONFIG_DIR:-$APPDIR/usr/share/alsa}"
ALSA_CONFIG_PATH="${ALSA_CONFIG_PATH:-$ALSA_CONFIG_DIR/alsa.conf}"
export ALSA_CONFIG_DIR ALSA_CONFIG_PATH
exec "$APPDIR/AppRun.linuxdeploy" "$@"
EOF
chmod +x "$appdir/AppRun"
for library in avformat avcodec avutil swresample; do
    if ! find "$appdir/usr" -name "lib${library}.so.*" -print -quit | grep -q .; then
        printf 'FFmpeg library lib%s was not bundled\n' "$library" >&2
        exit 1
    fi
done

# Keep the AppDir entry files explicit; the user data and original installer are never staged.
ln -sfn usr/share/applications/io.github.openlf2.OpenLF2.desktop \
    "$appdir/io.github.openlf2.OpenLF2.desktop"
# The AppDir's icon is the PNG rendered from the project's SVG (tools/icons/make_icons.py).
ln -sfn usr/share/icons/hicolor/256x256/apps/io.github.openlf2.OpenLF2.png \
    "$appdir/io.github.openlf2.OpenLF2.png"
ln -sfn io.github.openlf2.OpenLF2.png "$appdir/.DirIcon"
test -x "$appdir/AppRun"
test -f "$appdir/usr/bin/scripts/base/package.manifest"
test -f "$appdir/usr/share/metainfo/io.github.openlf2.OpenLF2.metainfo.xml"

output="$PWD/out/OpenLF2-${appimage_arch}.AppImage"
ARCH="$appimage_arch" APPIMAGE_EXTRACT_AND_RUN=1 \
    "$tools_dir/$tool_asset" --runtime-file "$runtime_file" "$appdir" "$output"
env -u LD_LIBRARY_PATH APPIMAGE_EXTRACT_AND_RUN=1 "$output" --help
(cd "$(dirname "$output")" && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")
