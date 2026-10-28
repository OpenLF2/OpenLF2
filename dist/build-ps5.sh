#!/usr/bin/env bash
# Builds the PS5 title folder and packs it as out/OpenLF2-PS5-x64.zip.
# Needs on the host: clang-18, lld-18, cmake, ninja, pkg-config, wget, unzip, zip, python3.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/ps5"
native="$build_dir/vendor/ps5-native-app-boilerplate"
boilerplate_url='https://github.com/drakmor/ps5-native-app-boilerplate.git'
boilerplate_commit=470695e0c557f99ff2df0e36e4df713c5e636526

# The boilerplate supplies the ELF converter, the libc.prx runtime shim (generated from its
# source) and, through its pinned and checksummed downloads, the ps5-payload-sdk and PacBrew ports.
mkdir -p "$build_dir/vendor"
if [[ ! -d "$native/.git" ]]; then
    git clone --quiet "$boilerplate_url" "$native"
    git -C "$native" -c advice.detachedHead=false checkout --quiet --detach "$boilerplate_commit"
fi
[[ $(git -C "$native" rev-parse HEAD) == "$boilerplate_commit" ]]
make -C "$native" pacbrew
# The default target generates libc.prx, fetches the SDK and builds ps5-native-tool as it assembles its demo app.
make -C "$native"

cmake -S "$repo_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$native/.deps/native/ps5-payload-sdk/toolchain/prospero.cmake" \
    -DOPENLF2_PS5_PACBREW_ROOT="$native/.deps/pacbrew/v0.40.2/sysroot" \
    -DOPENLF2_PS5_NATIVE_TOOL="$native/build/host/ps5-native-tool" \
    -DOPENLF2_PS5_RUNTIME_MODULE="$native/runtime/libc.prx" \
    -DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config -DPKG_CONFIG_ARGN=--static
cmake --build "$build_dir"

title=$(basename "$(find "$build_dir/app" -mindepth 1 -maxdepth 1 -type d -name "PPSA*")")
stage="$build_dir/package"
rm -rf "$stage"
mkdir -p "$stage" "$repo_root/out"
cp -r "$build_dir/app/$title" "$stage/$title"
cp "$repo_root/LICENSE" "$repo_root/THIRD-PARTY-NOTICES.md" "$stage/"
rm -f "$repo_root/out/OpenLF2-PS5-x64.zip"
(cd "$stage" && zip -qr "$repo_root/out/OpenLF2-PS5-x64.zip" .)
