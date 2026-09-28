// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/digest.hpp"
#include <array>
#ifdef __SWITCH__
#include <mbedtls/sha256.h>
#else
#include <openssl/evp.h>
#endif

namespace openlf2 {
Result<std::string> sha256(std::span<const char> input) {
    std::array<unsigned char, 32> digest{};
#ifdef __SWITCH__
    if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(input.data()), input.size(),
                           digest.data(), 0) != 0) {
        return fail(ErrorCode::dependency, "SHA-256 failed");
    }
#else
    unsigned int size = 0;
    if (EVP_Digest(input.data(), input.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1 ||
        size != digest.size()) return fail(ErrorCode::dependency, "SHA-256 failed");
#endif
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (const auto byte : digest) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}
}
