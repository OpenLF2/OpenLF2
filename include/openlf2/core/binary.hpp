// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include "openlf2/core/result.hpp"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace openlf2 {
// Parser-local exceptions are translated to Result at resource boundaries.
class FormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class BinaryView {
public:
    explicit BinaryView(std::span<const char> bytes) : bytes_(bytes) {}
    // Calling slice only for its bounds validation is also intentional.
    auto slice(std::size_t offset, std::size_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - offset) {
            throw FormatError("binary range outside input at " + std::to_string(offset));
        }
        return bytes_.subspan(offset, size);
    }
    [[nodiscard]] std::uint8_t u8(std::size_t offset) const {
        return static_cast<std::uint8_t>(slice(offset, 1).front());
    }
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const {
        slice(offset, 2);
        return static_cast<std::uint16_t>(u8(offset) | (std::uint16_t{u8(offset + 1)} << 8));
    }
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const {
        slice(offset, 4);
        std::uint32_t value = 0;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            value |= std::uint32_t{u8(offset + byte)} << (byte * 8);
        }
        return value;
    }
    [[nodiscard]] std::string text(std::size_t offset, std::size_t size) const {
        const auto data = slice(offset, size);
        return {data.begin(), data.end()};
    }
private:
    std::span<const char> bytes_;
};
}
