// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/resources/data_file.hpp"
#include <string>
#include <string_view>

namespace openlf2 {
Result<Bytes> decode_data_file(std::span<const char> encoded) {
    constexpr std::string_view key = "SiuHungIsAGoodBearBecauseHeIsVeryGood";
    constexpr std::size_t prefix = 123;
    if (encoded.size() < prefix) return fail(ErrorCode::format, "data file shorter than prefix");
    Bytes result;
    result.reserve(encoded.size() - prefix);
    for (std::size_t index = prefix; index < encoded.size(); ++index) {
        const auto byte = static_cast<unsigned char>(encoded[index]);
        const auto mask = static_cast<unsigned char>(key[index % key.size()]);
        result.push_back(static_cast<char>((static_cast<unsigned int>(byte) + 256 - mask) & 255));
    }
    return result;
}

Result<Bytes> read_data_text(const ResourceSource& resources, std::string_view path, std::size_t limit) {
    auto bytes = resources.read(path);
    if (!bytes) return std::unexpected(bytes.error());
    if (bytes->size() > limit) return fail(ErrorCode::limit, std::string(path) + ": exceeds size limit");
    const auto encoded = path.size() >= 3 && (path[path.size() - 3] | 0x20) == 'd' &&
                         (path[path.size() - 2] | 0x20) == 'a' && (path[path.size() - 1] | 0x20) == 't';
    if (encoded) {
        auto decoded = decode_data_file(*bytes);
        if (!decoded) return std::unexpected(decoded.error());
        bytes = std::move(*decoded);
    }
    for (const char character : *bytes) {
        if (character == '\0' || character == '\x1a') {
            return fail(ErrorCode::unsupported, std::string(path) + ": NUL or 0x1A byte in data text");
        }
    }
    return bytes;
}
}
