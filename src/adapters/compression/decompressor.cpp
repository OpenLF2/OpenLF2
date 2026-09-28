// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/compression.hpp"
#include <array>
#include <limits>
#include <bzlib.h>
#include <zlib.h>

namespace openlf2 {
namespace {
class ZlibStream {
public:
    z_stream state{};
    bool initialized = false;
    ~ZlibStream() { if (initialized) inflateEnd(&state); }
};
class BzipStream {
public:
    bz_stream state{};
    bool initialized = false;
    ~BzipStream() { if (initialized) BZ2_bzDecompressEnd(&state); }
};
class StreamDecompressor final : public Decompressor {
public:
    Result<Bytes> decode(Compression method, std::span<const char> input,
                         std::size_t expected_size) const override {
        constexpr std::size_t maximum = 64 * 1024 * 1024;
        if (expected_size > maximum || input.size() > maximum) {
            return fail(ErrorCode::limit, "compressed entry exceeds 64 MiB limit");
        }
        if (method == Compression::stored) {
            if (input.size() != expected_size) return fail(ErrorCode::format, "stored size mismatch");
            return Bytes(input.begin(), input.end());
        }
        // One extra output byte detects expansion beyond the declared size.
        Bytes output(expected_size + 1);
        if (method == Compression::zlib) {
            ZlibStream stream;
            if (inflateInit(&stream.state) != Z_OK) return fail(ErrorCode::dependency, "zlib init failed");
            stream.initialized = true;
            stream.state.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
            stream.state.avail_in = static_cast<uInt>(input.size());
            stream.state.next_out = reinterpret_cast<Bytef*>(output.data());
            stream.state.avail_out = static_cast<uInt>(output.size());
            const auto status = inflate(&stream.state, Z_FINISH);
            if (status != Z_STREAM_END || stream.state.avail_in != 0 ||
                stream.state.total_out != expected_size) {
                return fail(ErrorCode::format, "invalid zlib stream or decoded size");
            }
        } else if (method == Compression::bzip2) {
            BzipStream stream;
            if (BZ2_bzDecompressInit(&stream.state, 0, 0) != BZ_OK) {
                return fail(ErrorCode::dependency, "bzip2 init failed");
            }
            stream.initialized = true;
            stream.state.next_in = const_cast<char*>(input.data());
            stream.state.avail_in = static_cast<unsigned int>(input.size());
            stream.state.next_out = output.data();
            stream.state.avail_out = static_cast<unsigned int>(output.size());
            int status = BZ_OK;
            do {
                const auto before_input = stream.state.avail_in;
                const auto before_output = stream.state.avail_out;
                status = BZ2_bzDecompress(&stream.state);
                if (status == BZ_OK && before_input == stream.state.avail_in &&
                    before_output == stream.state.avail_out) break;
            } while (status == BZ_OK && stream.state.avail_out != 0);
            if (status != BZ_STREAM_END || stream.state.avail_in != 0 ||
                stream.state.total_out_hi32 != 0 || stream.state.total_out_lo32 != expected_size) {
                return fail(ErrorCode::format, "invalid bzip2 stream or decoded size");
            }
        } else return fail(ErrorCode::unsupported, "unsupported compression method");
        output.resize(expected_size);
        return output;
    }
};
class ZlibCompressor final : public Compressor {
public:
    Result<Bytes> encode(std::span<const char> input) const override {
        if (input.size() > 64 * 1024 * 1024) return fail(ErrorCode::limit, "compression input exceeds 64 MiB limit");
        uLongf size = compressBound(static_cast<uLong>(input.size()));
        Bytes output(size);
        const auto status = compress(reinterpret_cast<Bytef*>(output.data()), &size,
                                     reinterpret_cast<const Bytef*>(input.data()), static_cast<uLong>(input.size()));
        if (status != Z_OK) return fail(ErrorCode::dependency, "zlib compress failed");
        output.resize(size);
        return output;
    }
};
}
std::unique_ptr<Decompressor> make_decompressor() { return std::make_unique<StreamDecompressor>(); }
std::unique_ptr<Compressor> make_compressor() { return std::make_unique<ZlibCompressor>(); }
}
