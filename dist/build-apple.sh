#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
target="${1:?usage: dist/build-apple.sh macos-x64|macos-arm64|ios-device-arm64}"
case "$target" in
    macos-x64) os=Macos; arch=x86_64; sdk=macosx; cmake_arch=x86_64 ;;
    macos-arm64) os=Macos; arch=armv8; sdk=macosx; cmake_arch=arm64 ;;
    ios-device-arm64) os=iOS; arch=armv8; sdk=iphoneos; cmake_arch=arm64 ;;
    *) echo "Unsupported Apple target: $target" >&2; exit 2 ;;
esac

build="$root/build/apple/$target"
mkdir -p "$build" "$root/out"
profile="$build/conan-profile"
sdk_path="$(xcrun --sdk "$sdk" --show-sdk-path)"
conan profile detect --force
if [[ "$os" == iOS ]]; then
    cat > "$profile" <<EOF
include(default)

[settings]
os=iOS
os.version=15.0
os.sdk=$sdk
arch=$arch
compiler.cppstd=23
build_type=Release

[conf]
tools.apple:sdk_path=$sdk_path
tools.cmake.cmaketoolchain:generator=Ninja
EOF
    deployment_target=15.0
    conan_options=(-o:h 'sdl/*:shared=False' -o:h 'ffmpeg/*:shared=False'
        -o:h 'libcurl/*:shared=False' -o:h 'openssl/*:shared=False')
else
    cat > "$profile" <<EOF
include(default)

[settings]
os=Macos
os.version=13.0
arch=$arch
compiler.cppstd=23
build_type=Release

[conf]
tools.cmake.cmaketoolchain:generator=Ninja
EOF
    deployment_target=13.0
fi

conan_archive="$build/conan-cache.tgz"
if [[ -f "$conan_archive" ]]; then
    conan cache restore "$conan_archive"
fi
conan export conan/luajit
conan_args=(. --profile:host "$profile" --profile:build default
    --output-folder "$build/conan" --build=missing
    --deployer=runtime_deploy --deployer-folder "$build/runtime")
if [[ "$os" == iOS ]]; then
    conan_args+=("${conan_options[@]}")
fi
conan install "${conan_args[@]}"
conan cache save '*:*' --no-source --file="$build/conan-cache-next.tgz"
mv "$build/conan-cache-next.tgz" "$conan_archive"

cmake_args=(-S . -B "$build/native" -G Ninja
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$build/conan/conan_toolchain.cmake"
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$cmake_arch"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target"
    -DOPENLF2_DEPENDENCY_PROVIDER=conan)
if [[ "$os" == iOS ]]; then
    cmake_args+=(-DCMAKE_SYSTEM_NAME=iOS)
fi
cmake "${cmake_args[@]}"
if [[ "$os" == iOS ]]; then
    cmake --build "$build/native" --target openlf2 --parallel 3
else
    cmake --build "$build/native" --parallel 3
fi

bundle="$build/native/openlf2.app"
if [[ ! -d "$bundle" ]]; then
    echo "Apple app bundle was not built: $bundle" >&2
    exit 1
fi
if [[ "$os" == iOS ]]; then
    resources="$bundle"
    # The home-screen icons, rendered from the project's SVG by CMake (tools/icons/make_icons.py).
    cp "$build/native/icons/ios/"*.png "$bundle/"
    test -f "$bundle/AppIcon60x60@2x.png"
    plutil -lint "$bundle/Info.plist"
else
    resources="$bundle/Contents/Resources"
    test -f "$resources/openlf2.icns"
    cmake -DBUNDLE_PATH="$bundle" -DDEPLOY_ROOT="$build/runtime" \
        -P dist/fixup-macos.cmake
fi
mkdir -p "$resources/scripts"
cp -R scripts/. "$resources/scripts/"
cp README.md LICENSE "$resources/"
python3 dist/collect-licenses.py "$resources/licenses" \
    --key ffmpeg --key ffmpeg-notes --key sdl3 --key luajit --key zlib --key bzip2 \
    --key openssl --key libcurl
test -f "$resources/scripts/runtime/bootstrap.lua"

if [[ "$os" == iOS ]]; then
    ipa_root="$build/ipa-staging"
    rm -rf "$ipa_root"
    mkdir -p "$ipa_root/Payload"
    ditto "$bundle" "$ipa_root/Payload/openlf2.app"
    artifact="$root/out/OpenLF2-iOS-arm64-unsigned.ipa"
    (cd "$ipa_root" && ditto -c -k --sequesterRsrc --keepParent Payload "$artifact")
    unzip -tqq "$artifact"
    unzip -Z1 "$artifact" | grep -Fx 'Payload/openlf2.app/Info.plist' >/dev/null
else
    artifact="$root/out/OpenLF2-$target.app.zip"
    ditto -c -k --sequesterRsrc --keepParent "$bundle" "$artifact"
fi
(cd "$root/out" && shasum -a 256 "$(basename "$artifact")") > "$artifact.sha256"
echo "Created $artifact"
