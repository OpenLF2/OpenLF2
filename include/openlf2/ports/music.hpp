// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <cstdint>
#include <memory>
#include <span>

namespace openlf2 {
// A decoded music track as interleaved 16-bit stereo samples at 44100 Hz. Used by one thread.
class MusicStream {
public:
    virtual ~MusicStream() = default;
    // Fills `samples` (an even count: left, right, ...) and returns how many were written;
    // 0 means the end of the track.
    virtual Result<std::size_t> read(std::span<std::int16_t> samples) = 0;
    // Returns to the start of the track.
    virtual Result<void> rewind() = 0;
};
// Opens music files (the original's bgm/*.wma, played through DirectShow).
class MusicDecoder {
public:
    virtual ~MusicDecoder() = default;
    // `data` is the whole file; the stream keeps its own copy.
    virtual Result<std::unique_ptr<MusicStream>> open(Bytes data) const = 0;
};
inline constexpr int music_rate = 44100;
// The FFmpeg adapter, or a decoder that reports that music support was not built.
std::unique_ptr<MusicDecoder> make_music_decoder();
}
