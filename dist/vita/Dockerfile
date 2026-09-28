FROM vitasdk/vitasdk@sha256:b409bd9752824c88b76aac628dd9a497e3bd7d35860c933f9bd327b645efec33

# LuaJIT's 32-bit Vita VM generator requires a 32-bit native host compiler.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    --no-install-recommends gcc-multilib && rm -rf /var/lib/apt/lists/*

# The image's curl archive uses an older OpenSSL ABI than its crypto archive.
# VitaSDK's current mbedTLS variant replaces that mismatched package.
RUN printf 'y\ny\n' | vdpm install curl-mbedtls \
    && vdpm list curl-mbedtls | grep -Fx 'curl-mbedtls 8.22.0-1'
