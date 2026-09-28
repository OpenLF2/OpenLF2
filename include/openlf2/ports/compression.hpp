// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <memory>
#include <span>
#include <cstdint>

namespace openlf2 {
enum class Compression : std::uint8_t { stored, zlib, bzip2 };
class Decompressor {
public:
    virtual ~Decompressor() = default;
    virtual Result<Bytes> decode(Compression method, std::span<const char> input,
                                 std::size_t expected_size) const = 0;
};
std::unique_ptr<Decompressor> make_decompressor();
// zlib's compress() format (a zlib stream at the default level), as the original's recordings use.
class Compressor {
public:
    virtual ~Compressor() = default;
    virtual Result<Bytes> encode(std::span<const char> input) const = 0;
};
std::unique_ptr<Compressor> make_compressor();
}
