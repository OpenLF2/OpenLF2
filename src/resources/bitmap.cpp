// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/resources/bitmap.hpp"
#include "openlf2/core/binary.hpp"
#include <optional>

namespace openlf2::bitmap {
namespace {
constexpr std::size_t header_size = 40;
constexpr std::uint32_t uncompressed = 0;
constexpr std::uint32_t run_length_8 = 1;

void append_u32(Bytes& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) output.push_back(static_cast<char>((value >> shift) & 255));
}
void store_u32(Bytes& output, std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4; ++byte) {
        output[offset + byte] = static_cast<char>((value >> (byte * 8)) & 255);
    }
}

// Expands bottom-up RLE8. Pixels skipped by delta or line escapes stay index 0.
Bytes expand_rle8(const BinaryView& stream, std::size_t stream_size, std::size_t width,
                  std::size_t height, std::size_t stride) {
    Bytes pixels(stride * height, '\0');
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t position = 0;
    const auto write_span = [&](std::size_t count) {
        if (y >= height || count > width - x) throw FormatError("RLE8 pixels outside the bitmap");
    };
    for (;;) {
        if (stream_size - position < 2) throw FormatError("RLE8 data ends without end-of-bitmap");
        const std::size_t count = stream.u8(position);
        const auto value = stream.u8(position + 1);
        position += 2;
        if (count != 0) {
            write_span(count);
            for (std::size_t index = 0; index < count; ++index) {
                pixels[y * stride + x++] = static_cast<char>(value);
            }
        } else if (value == 0) {
            x = 0;
            ++y;
        } else if (value == 1) {
            return pixels;
        } else if (value == 2) {
            if (stream_size - position < 2) throw FormatError("truncated RLE8 delta");
            x += stream.u8(position);
            y += stream.u8(position + 1);
            position += 2;
            if (x > width || y > height) throw FormatError("RLE8 delta outside the bitmap");
        } else {
            const std::size_t literal = value;
            const auto padded = literal + (literal & 1);
            if (stream_size - position < padded) throw FormatError("truncated RLE8 literal");
            write_span(literal);
            for (std::size_t index = 0; index < literal; ++index) {
                pixels[y * stride + x++] = static_cast<char>(stream.u8(position + index));
            }
            position += padded;
        }
    }
}

Bytes convert(std::span<const char> dib, std::optional<std::size_t> pixel_offset) {
    const BinaryView header(dib);
    if (header.u32(0) != header_size || header.u16(12) != 1) {
        throw FormatError("only BITMAPINFOHEADER bitmaps with one plane are supported");
    }
    const auto bits = header.u16(14);
    const auto compression = header.u32(16);
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32) {
        throw FormatError("unsupported bitmap bit depth");
    }
    if (compression != uncompressed && !(compression == run_length_8 && bits == 8)) {
        throw FormatError("unsupported bitmap compression");
    }
    const auto colors = header.u32(32) != 0 ? header.u32(32) : (bits <= 8 ? (1U << bits) : 0U);
    if (colors > 256) throw FormatError("oversized bitmap palette");
    const std::size_t palette_end = header_size + std::size_t{colors} * 4;
    header.slice(0, palette_end);
    const auto width = header.u32(4);
    const auto height_bits = header.u32(8);
    const bool top_down = height_bits >= 0x80000000U;
    const std::uint64_t height = top_down ? std::uint64_t{0x100000000ULL} - height_bits : height_bits;
    if (width == 0 || width > 8192 || height == 0 || height > 8192) {
        throw FormatError("bitmap dimensions exceed limits");
    }
    if (top_down && compression == run_length_8) throw FormatError("top-down RLE8 bitmap");
    const auto stride = static_cast<std::size_t>(((std::uint64_t{width} * bits + 31) / 32) * 4);
    const auto pixel_bytes = stride * static_cast<std::size_t>(height);
    const auto start = pixel_offset.value_or(palette_end);
    if (start < palette_end || start > dib.size()) throw FormatError("bitmap pixels overlap header");

    Bytes pixels;
    if (compression == run_length_8) {
        const auto stream = header.slice(start, dib.size() - start);
        pixels = expand_rle8(BinaryView(stream), stream.size(), width, static_cast<std::size_t>(height), stride);
    } else {
        const auto stored = header.slice(start, pixel_bytes);
        pixels.assign(stored.begin(), stored.end());
    }

    Bytes output{'B', 'M'};
    append_u32(output, static_cast<std::uint32_t>(14 + palette_end + pixels.size()));
    append_u32(output, 0);
    append_u32(output, static_cast<std::uint32_t>(14 + palette_end));
    const auto info = header.slice(0, palette_end);
    output.insert(output.end(), info.begin(), info.end());
    store_u32(output, 14 + 16, uncompressed);
    store_u32(output, 14 + 20, static_cast<std::uint32_t>(pixels.size()));
    output.insert(output.end(), pixels.begin(), pixels.end());
    return output;
}
}

Result<Bytes> from_dib(std::span<const char> dib) {
    try { return convert(dib, std::nullopt); }
    catch (const FormatError& error) { return fail(ErrorCode::format, error.what()); }
}

Result<Bytes> from_file(std::span<const char> file) {
    try {
        const BinaryView view(file);
        if (view.text(0, 2) != "BM") throw FormatError("missing BMP signature");
        const std::size_t pixels = view.u32(10);
        if (pixels < 14) throw FormatError("bitmap pixels overlap file header");
        return convert(view.slice(14, file.size() - 14), pixels - 14);
    } catch (const FormatError& error) { return fail(ErrorCode::format, error.what()); }
}
}
