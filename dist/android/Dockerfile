# Android APK toolchain for dist/build-android-docker.sh. It reproduces the
# .github/workflows/android.yml job (Ubuntu 24.04, OpenJDK 17, Gradle 8.13, SDK
# 35, NDK r28c, Conan 2.32.0, CMake/Ninja/pkg-config), so the cross build and the
# Gradle packaging need no SDK, NDK or Conan installation on the workstation.
FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3

# Published by Gradle itself: services.gradle.org/distributions/gradle-8.13-bin.zip.sha256.
ARG GRADLE_VERSION=8.13
ARG GRADLE_SHA256=20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78
# Google publishes the SDK archive digests in its own repository manifest,
# https://dl.google.com/android/repository/repository2-1.xml (cmdline-tools;latest = 23.0).
ARG CMDLINE_TOOLS_URL=https://dl.google.com/android/repository/commandlinetools-linux-16111833_latest.zip
ARG CMDLINE_TOOLS_SHA1=e025545c62a8e64c7559119566a569fb1dec5f60
ARG CONAN_VERSION=2.32.0

# The APK build runs as the calling user's UID, so caches live under /tmp, not in
# root's home directory, exactly like the host-side scripts assume.
ENV DEBIAN_FRONTEND=noninteractive \
    ANDROID_HOME=/opt/android-sdk \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    GRADLE_HOME=/opt/gradle \
    CONAN_HOME=/tmp/conan2 \
    GRADLE_USER_HOME=/tmp/gradle-home \
    HOME=/tmp

# gcc-multilib is the 32-bit host compiler LuaJIT needs for armeabi-v7a and x86,
# and the -dev packages keep Conan's build-context system requirements satisfied
# without an apt run inside the container.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl file gcc-multilib git \
        libbz2-dev ninja-build openjdk-17-jdk-headless pkg-config \
        python3 python3-pip python3-venv unzip zip zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p "$ANDROID_HOME/cmdline-tools" \
    && curl --fail --location --retry 3 --output /tmp/cmdline-tools.zip "$CMDLINE_TOOLS_URL" \
    && echo "$CMDLINE_TOOLS_SHA1  /tmp/cmdline-tools.zip" | sha1sum --check \
    && unzip -q /tmp/cmdline-tools.zip -d "$ANDROID_HOME/cmdline-tools" \
    && mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest" \
    && rm /tmp/cmdline-tools.zip

ENV PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:/opt/gradle/bin:/opt/conan-venv/bin:$PATH

# The same components the CI job installs, at the same revisions.
RUN yes | sdkmanager --licenses > /dev/null; \
    sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'ndk;28.2.13676358'

# sdkmanager's extraction keeps only the owner's execute bit, so the NDK's 0755
# clang wrappers and tools arrive as 0744 and stay unrunnable for the non-root UID
# this image is built to run as. The archives themselves are correct; a+rX only
# adds what is already there, and also leaves the SDK root writable for Gradle.
RUN chmod -R a+rX "$ANDROID_HOME" && chmod a+rwx "$ANDROID_HOME"

RUN curl --fail --location --retry 3 --output /tmp/gradle.zip \
        "https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip" \
    && echo "$GRADLE_SHA256  /tmp/gradle.zip" | sha256sum --check \
    && unzip -q /tmp/gradle.zip -d /opt && mv "/opt/gradle-$GRADLE_VERSION" "$GRADLE_HOME" \
    && rm /tmp/gradle.zip

# Conan 2 in a virtual environment, as in CI.
RUN python3 -m venv /opt/conan-venv \
    && /opt/conan-venv/bin/pip install --no-cache-dir "conan==$CONAN_VERSION"
