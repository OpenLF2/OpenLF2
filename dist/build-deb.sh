#!/usr/bin/env bash
# Build a native Debian package inside the architecture-matched Debian 13 CI container.
# Library headers and runtime dependencies come from Debian; Conan is not used here.
set -euo pipefail

case "${1:-}" in
    x86) expected_arch=i386 ;;
    x64) expected_arch=amd64 ;;
    arm64) expected_arch=arm64 ;;
    *)
        printf 'usage: %s x86|x64|arm64\n' "$0" >&2
        exit 2
        ;;
esac

if [[ $(dpkg --print-architecture) != "$expected_arch" ]]; then
    printf 'expected Debian architecture %s\n' "$expected_arch" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    appstream ca-certificates cmake desktop-file-utils dpkg-dev g++ \
    libavcodec-dev libavformat-dev libavutil-dev libbz2-dev \
    libcurl4-openssl-dev libluajit-5.1-dev libsdl3-dev libssl-dev \
    libswresample-dev ninja-build pkg-config python3 zlib1g-dev

desktop-file-validate dist/io.github.openlf2.OpenLF2.desktop
appstreamcli validate --no-net dist/io.github.openlf2.OpenLF2.metainfo.xml

build_dir="$PWD/build/deb-$expected_arch"
package_root="$build_dir/package"
mkdir -p "$build_dir" out
cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DOPENLF2_DEPENDENCY_PROVIDER=system
cmake --build "$build_dir" --parallel "$(nproc)"
DESTDIR="$package_root" cmake --install "$build_dir"
{
    printf 'Upstream-Name: OpenLF2\n'
    printf 'Source: https://github.com/OpenLF2/OpenLF2\n'
    printf 'Copyright: 2026 OpenLF2 contributors\n'
    printf 'License: MIT\n\n'
    cat LICENSE
    printf '\nThe executable links Debian FFmpeg libraries built with GPL components.\n'
    printf 'The combined binary is distributed under GPL-2-or-later; see\n'
    printf '/usr/share/common-licenses/GPL-2 and the FFmpeg package copyright files.\n'
} > "$package_root/usr/share/doc/openlf2/copyright"
test -s "$package_root/usr/share/doc/openlf2/copyright"

test -x "$package_root/usr/bin/openlf2"
test -f "$package_root/usr/bin/scripts/base/package.manifest"
test -f "$package_root/usr/share/applications/io.github.openlf2.OpenLF2.desktop"
test -f "$package_root/usr/share/metainfo/io.github.openlf2.OpenLF2.metainfo.xml"
"$package_root/usr/bin/openlf2" --help

# dpkg-shlibdeps reads Debian's shlibs/symbols metadata and emits minimum runtime
# versions for the exact libraries linked by this architecture's executable.
mkdir -p "$build_dir/debian" "$package_root/DEBIAN"
cp dist/debian/source-control "$build_dir/debian/control"
depends=$(
    cd "$build_dir"
    dpkg-shlibdeps -O "$package_root/usr/bin/openlf2" |
        sed -n 's/^shlibs:Depends=//p'
)
if [[ -z "$depends" ]]; then
    printf 'dpkg-shlibdeps returned no runtime dependencies\n' >&2
    exit 1
fi

version=$(sed -n 's/^project(OpenLF2 VERSION \([^ ]*\) LANGUAGES CXX)$/\1/p' CMakeLists.txt)
test -n "$version"
while IFS= read -r line || [[ -n "$line" ]]; do
    line=${line//@VERSION@/$version-1}
    line=${line//@ARCH@/$expected_arch}
    line=${line//@DEPENDS@/$depends}
    printf '%s\n' "$line"
done < dist/debian/control.in > "$package_root/DEBIAN/control"

output="$PWD/out/openlf2_${version}-1_${expected_arch}.deb"
dpkg-deb --build --root-owner-group "$package_root" "$output"
(cd "$(dirname "$output")" && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")
