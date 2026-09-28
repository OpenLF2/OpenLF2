#!/usr/bin/env bash
set -euo pipefail

# Build from the repository root after installing SDK 35, NDK r28c, Gradle 8.13,
# Conan 2.32, CMake and Ninja. No original game files are used or packaged.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
abi="${1:?usage: dist/build-android.sh armeabi-v7a|arm64-v8a|x86|x86_64}"
lua_target_cflags=
case "$abi" in
    armeabi-v7a) conan_arch=armv7; ndk_triple=arm-linux-androideabi;
        clang_triple=armv7a-linux-androideabi; host_cc='gcc\ -m32';
        lua_target_cflags=' TARGET_CFLAGS=-Dfseeko=fseek\ -Dftello=ftell' ;;
    arm64-v8a) conan_arch=armv8; ndk_triple=aarch64-linux-android;
        clang_triple=aarch64-linux-android; host_cc=gcc ;;
    x86) conan_arch=x86; ndk_triple=i686-linux-android;
        clang_triple=i686-linux-android; host_cc='gcc\ -m32';
        lua_target_cflags=' TARGET_CFLAGS=-Dfseeko=fseek\ -Dftello=ftell' ;;
    x86_64) conan_arch=x86_64; ndk_triple=x86_64-linux-android;
        clang_triple=x86_64-linux-android; host_cc=gcc ;;
    *) echo "Unsupported Android ABI: $abi" >&2; exit 2 ;;
esac

sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$sdk" ]]; then echo 'Set ANDROID_HOME to the Android SDK.' >&2; exit 2; fi
ndk="${OPENLF2_ANDROID_NDK:-$sdk/ndk/28.2.13676358}"
if [[ ! -f "$ndk/build/cmake/android.toolchain.cmake" ]]; then
    echo "Android NDK r28c was not found at $ndk" >&2; exit 2
fi
if ! grep -Eq '^Pkg.Revision[[:space:]]*=[[:space:]]*28\.2\.13676358[[:space:]]*$' "$ndk/source.properties"; then
    echo "Android NDK r28c (28.2.13676358) is required: $ndk" >&2; exit 2
fi
ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
target_cc="$ndk_bin/${clang_triple}21-clang"
target_cxx="$ndk_bin/${clang_triple}21-clang++"
target_strip="$ndk_bin/llvm-strip"
for tool in "$target_cc" "$target_cxx" "$target_strip"; do
    if [[ ! -x "$tool" ]]; then echo "NDK tool was not found: $tool" >&2; exit 2; fi
done
clang_version="$($ndk_bin/clang --version | sed -n '1s/.*version \([0-9][0-9]*\).*/\1/p')"
if [[ -z "$clang_version" ]]; then echo 'Cannot read NDK Clang version.' >&2; exit 2; fi

build="$root/build/android/$abi"
mkdir -p "$build" "$root/build/android/jniLibs/$abi" "$root/build/android/assets" "$root/build/android/sdl-java" "$root/out"
profile="$build/conan-profile"
cat > "$profile" <<EOF
[settings]
os=Android
os.api_level=21
arch=$conan_arch
compiler=clang
compiler.version=$clang_version
compiler.libcxx=c++_shared
compiler.cppstd=23
build_type=Release

[conf]
tools.android:ndk_path=$ndk
tools.cmake.cmaketoolchain:generator=Ninja
user.sdl:android=True
# FFmpeg's configure script ignores CC/CXX in the environment; its Conan recipe
# reads this package-specific setting to form --cc and --cxx.
ffmpeg/*:tools.build:compiler_executables={'c': '$target_cc', 'cpp': '$target_cxx'}

[buildenv]
# LuaJIT's Makefile assigns CC itself, so Conan's CC environment is ignored.
# GNU Make reads command-line assignments in MAKEFLAGS ahead of Makefile assignments.
luajit/*:MAKEFLAGS=CC=$target_cc HOST_CC=$host_cc TARGET_SYS=Linux TARGET_STRIP=$target_strip CFLAGS=-fPIC LDFLAGS=$lua_target_cflags
# Without this, FFmpeg's configure falls back to the host's strip instead of the NDK's,
# which fails once shared=True makes it strip its own .so files at install time.
ffmpeg/*:STRIP=$target_strip
EOF

conan profile detect --force
conan_archive="$build/conan-cache.tgz"
if [[ -f "$conan_archive" ]]; then
    conan cache restore "$conan_archive"
fi
conan export conan/luajit
conan install . --profile:host "$profile" --profile:build default \
    --output-folder "$build/conan" --build=missing \
    --deployer=runtime_deploy --deployer-folder "$build/runtime" \
    -o:h 'ffmpeg/*:shared=True' \
    -c 'user.sdl:android=True' \
    -c:b 'tools.system.package_manager:mode=install'
conan cache save '*:*' --no-source --file="$build/conan-cache-next.tgz"
mv "$build/conan-cache-next.tgz" "$conan_archive"

cmake -S . -B "$build/native" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$build/conan/conan_toolchain.cmake" \
    -DOPENLF2_DEPENDENCY_PROVIDER=conan
cmake --build "$build/native" --target openlf2

# Android's jniLibs expects unversioned .so names; FFmpeg's android build already produces those.
native_dir="$root/build/android/jniLibs/$abi"
find "$native_dir" -maxdepth 1 -type f -name '*.so' -delete
find "$build/runtime" \( -type f -o -type l \) -name '*.so' -exec cp -L '{}' "$native_dir/" \;
cp "$build/native/libmain.so" "$native_dir/libmain.so"
cp "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$ndk_triple/libc++_shared.so" "$native_dir/"
if [[ ! -f "$native_dir/libSDL3.so" ]]; then
    echo 'Conan did not deploy libSDL3.so.' >&2; exit 1
fi

# SDLActivity must match the version of the native SDL3 Conan dependency.
sdl_archive="$build/SDL3-3.4.14.tar.gz"
curl --fail --location --retry 3 \
    'https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-3.4.14.tar.gz' \
    --output "$sdl_archive"
echo "30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb  $sdl_archive" | sha256sum --check
tar -xzf "$sdl_archive" -C "$root/build/android/sdl-java" --strip-components=6 \
    SDL3-3.4.14/android-project/app/src/main/java/org/libsdl/app

# Launcher icons rendered from the project's single SVG (nothing of them is committed).
python3 "$root/tools/icons/make_icons.py" "$root/build/android/icons"
rm -rf "$root/build/android/res"
cp -R "$root/build/android/icons/android/res" "$root/build/android/res"
rm -rf "$root/build/android/assets/scripts"
cp -R "$root/scripts" "$root/build/android/assets/scripts"
python3 dist/collect-licenses.py "$root/build/android/assets/licenses" \
    --key ffmpeg --key ffmpeg-notes --key sdl3 --key luajit --key zlib --key bzip2 \
    --key openssl --key libcurl --local "ndk-runtime=$ndk/NOTICE"
gradle --no-daemon -p "$root/android" assembleDebug

artifact="$root/out/OpenLF2-Android-api21-$abi.apk"
cp "$root/android/app/build/outputs/apk/debug/app-debug.apk" "$artifact"
python3 - "$artifact" <<'PY'
import sys
from zipfile import ZipFile

with ZipFile(sys.argv[1]) as package:
    required = {"LICENSE-OpenLF2.txt", "LICENSE-ffmpeg.txt", "LICENSE-ffmpeg-notes.txt", "LICENSE-sdl3.txt",
                "LICENSE-luajit.txt", "LICENSE-zlib.txt", "LICENSE-bzip2.txt",
                "LICENSE-openssl.txt", "LICENSE-libcurl.txt", "LICENSE-ndk-runtime.txt"}
    missing = {"assets/licenses/" + name for name in required} - set(package.namelist())
    if missing:
        raise SystemExit(f"APK is missing license files: {sorted(missing)}")
PY
sha256sum "$artifact" > "$artifact.sha256"
echo "Created $artifact"
