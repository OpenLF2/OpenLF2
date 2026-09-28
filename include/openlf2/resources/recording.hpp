// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include "openlf2/ports/compression.hpp"
#include <cstdint>
#include <span>
#include <string_view>

namespace openlf2::recording {
// The uncompressed block: header, per-150-frame checksums, 10-byte frame records, text lines.
inline constexpr std::size_t block_size = 0x630e18;
inline constexpr std::uint32_t key_address = 0x44d7a0;
inline constexpr std::size_t key_capacity = 1346;
inline constexpr std::uint32_t version_address = 0x44d03c;

// Files under 1000 bytes, bad sizes and streams not inflating to exactly block_size are errors.
Result<Bytes> decode(std::span<const char> file, std::string_view key, const Decompressor& decompressor);
Result<Bytes> encode(std::span<const char> block, std::string_view key, const Compressor& compressor);
}
