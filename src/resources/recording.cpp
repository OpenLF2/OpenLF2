// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/resources/recording.hpp"
#include <algorithm>
#include <cstring>

namespace openlf2::recording {
namespace {
constexpr std::size_t file_limit = 16 * 1024 * 1024;
}

Result<Bytes> decode(std::span<const char> file, std::string_view key, const Decompressor& decompressor) {
    if (file.size() > file_limit) return fail(ErrorCode::limit, "recording file exceeds 16 MiB");
    if (file.size() < 1000) return fail(ErrorCode::format, "recording file is too short");
    std::uint32_t packed_size = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        packed_size |= std::uint32_t{static_cast<unsigned char>(file[index])} << (8 * index);
    }
    if (packed_size > file.size() - 4) return fail(ErrorCode::format, "recording packed size exceeds the file");
    Bytes packed(file.begin() + 4, file.begin() + 4 + static_cast<std::ptrdiff_t>(packed_size));
    const auto shifted = std::min(key.size(), packed.size());
    for (std::size_t index = 0; index < shifted; ++index) {
        packed[index] = static_cast<char>(static_cast<unsigned char>(packed[index]) - static_cast<unsigned char>(key[index]) + '0');
    }
    auto block = decompressor.decode(Compression::zlib, packed, block_size);
    if (!block) return fail(ErrorCode::format, "recording data is corrupted: " + block.error().message);
    return block;
}

Result<Bytes> encode(std::span<const char> block, std::string_view key, const Compressor& compressor) {
    if (block.size() != block_size) return fail(ErrorCode::format, "recording block has the wrong size");
    auto packed = compressor.encode(block);
    if (!packed) return std::unexpected(packed.error());
    const auto shifted = std::min(key.size(), packed->size());
    for (std::size_t index = 0; index < shifted; ++index) {
        (*packed)[index] = static_cast<char>(static_cast<unsigned char>((*packed)[index]) + static_cast<unsigned char>(key[index]) - '0');
    }
    Bytes file(4 + packed->size());
    const auto size = static_cast<std::uint32_t>(packed->size());
    for (std::size_t index = 0; index < 4; ++index) file[index] = static_cast<char>((size >> (8 * index)) & 0xff);
    std::copy(packed->begin(), packed->end(), file.begin() + 4);
    return file;
}
}
