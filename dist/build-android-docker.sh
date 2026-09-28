#!/usr/bin/env bash
set -euo pipefail

# Cross-builds an Android APK entirely inside a container, so that no Android SDK,
# NDK, JDK, Gradle or Conan installation is needed on the host. The container runs
# the unchanged dist/build-android.sh; the finished APK lands in out/.
# Usage: bash dist/build-android-docker.sh [arm64-v8a|armeabi-v7a|x86|x86_64]
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
abi="${1:-arm64-v8a}"

docker build --quiet --platform linux/amd64 -f "$root/dist/android/Dockerfile" \
    -t openlf2-android:local "$root/dist/android"
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
    -v "$root:/workspace" -w /workspace \
    openlf2-android:local \
    bash dist/build-android.sh "$abi"

printf 'APK: %s\n' "$root/out/OpenLF2-Android-api21-$abi.apk"
