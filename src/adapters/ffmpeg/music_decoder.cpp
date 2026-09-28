// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// FFmpeg adapter: decodes music files (the original's WMA v2 tracks) from memory into
// interleaved 16-bit stereo samples at 44100 Hz. FFmpeg types stay inside this file.
#include "openlf2/ports/music.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

namespace openlf2 {
namespace {
constexpr std::size_t music_limit = 64 * 1024 * 1024;
constexpr int io_buffer_size = 32 * 1024;

std::string describe(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}
std::unexpected<Error> ffmpeg_error(std::string_view operation, int code) {
    return fail(ErrorCode::format, "music: " + std::string(operation) + ": " + describe(code));
}

struct FreeIo {
    void operator()(AVIOContext* context) const noexcept {
        av_freep(&context->buffer);
        avio_context_free(&context);
    }
};
struct CloseFormat {
    void operator()(AVFormatContext* context) const noexcept { avformat_close_input(&context); }
};
struct FreeCodec {
    void operator()(AVCodecContext* context) const noexcept { avcodec_free_context(&context); }
};
struct FreeResampler {
    void operator()(SwrContext* context) const noexcept { swr_free(&context); }
};
struct FreePacket {
    void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
};
struct FreeFrame {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
struct FreeBuffer {
    void operator()(unsigned char* buffer) const noexcept { av_free(buffer); }
};

// The file bytes and read position behind the custom I/O context.
struct MemoryFile {
    Bytes bytes;
    std::size_t position = 0;
};
int read_memory(void* opaque, std::uint8_t* target, int size) {
    auto& file = *static_cast<MemoryFile*>(opaque);
    const auto left = file.bytes.size() - file.position;
    if (left == 0) return AVERROR_EOF;
    const auto count = std::min(left, static_cast<std::size_t>(size));
    std::memcpy(target, file.bytes.data() + file.position, count);
    file.position += count;
    return static_cast<int>(count);
}
std::int64_t seek_memory(void* opaque, std::int64_t offset, int whence) {
    auto& file = *static_cast<MemoryFile*>(opaque);
    const auto size = static_cast<std::int64_t>(file.bytes.size());
    if ((whence & AVSEEK_SIZE) != 0) return size;
    std::int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<std::int64_t>(file.position); break;
    case SEEK_END: base = size; break;
    default: return -1;
    }
    const auto target = base + offset;
    if (target < 0 || target > size) return -1;
    file.position = static_cast<std::size_t>(target);
    return target;
}

class FfmpegStream final : public MusicStream {
public:
    explicit FfmpegStream(Bytes bytes) : file_(std::make_unique<MemoryFile>(MemoryFile{std::move(bytes), 0})) {}

    Result<void> open() {
        std::unique_ptr<unsigned char, FreeBuffer> buffer(static_cast<unsigned char*>(av_malloc(io_buffer_size)));
        if (!buffer) return fail(ErrorCode::limit, "music: out of memory");
        io_.reset(avio_alloc_context(buffer.get(), io_buffer_size, 0, file_.get(), read_memory, nullptr, seek_memory));
        if (!io_) return fail(ErrorCode::limit, "music: out of memory");
        static_cast<void>(buffer.release()); // now owned by the I/O context (freed by FreeIo)
        AVFormatContext* format = avformat_alloc_context();
        if (format == nullptr) return fail(ErrorCode::limit, "music: out of memory");
        format->pb = io_.get();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        // avformat_open_input frees the context on failure.
        if (const int code = avformat_open_input(&format, nullptr, nullptr, nullptr); code < 0) {
            return ffmpeg_error("open", code);
        }
        format_.reset(format);
        if (const int code = avformat_find_stream_info(format_.get(), nullptr); code < 0) {
            return ffmpeg_error("stream info", code);
        }
        const AVCodec* codec = nullptr;
        stream_ = av_find_best_stream(format_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream_ < 0 || codec == nullptr) return ffmpeg_error("no audio stream", stream_ < 0 ? stream_ : AVERROR_DECODER_NOT_FOUND);
        codec_.reset(avcodec_alloc_context3(codec));
        if (!codec_) return fail(ErrorCode::limit, "music: out of memory");
        if (const int code = avcodec_parameters_to_context(codec_.get(), format_->streams[stream_]->codecpar); code < 0) {
            return ffmpeg_error("codec parameters", code);
        }
        if (const int code = avcodec_open2(codec_.get(), codec, nullptr); code < 0) return ffmpeg_error("codec", code);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        SwrContext* resampler = nullptr;
        if (const int code = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, music_rate,
                                                 &codec_->ch_layout, codec_->sample_fmt, codec_->sample_rate, 0, nullptr);
            code < 0) {
            return ffmpeg_error("resampler", code);
        }
        resampler_.reset(resampler);
        if (const int code = swr_init(resampler_.get()); code < 0) return ffmpeg_error("resampler", code);
        packet_.reset(av_packet_alloc());
        frame_.reset(av_frame_alloc());
        if (!packet_ || !frame_) return fail(ErrorCode::limit, "music: out of memory");
        return {};
    }

    Result<std::size_t> read(std::span<std::int16_t> samples) override {
        std::size_t written = 0;
        while (written + 1 < samples.size()) {
            if (offset_ < pending_.size()) {
                const auto count = std::min(pending_.size() - offset_, (samples.size() - written) & ~std::size_t{1});
                std::copy_n(pending_.begin() + static_cast<std::ptrdiff_t>(offset_), count,
                            samples.begin() + static_cast<std::ptrdiff_t>(written));
                offset_ += count;
                written += count;
                continue;
            }
            auto decoded = decode_more();
            if (!decoded) return std::unexpected(decoded.error());
            if (!*decoded) break;
        }
        return written;
    }

    Result<void> rewind() override {
        if (const int code = av_seek_frame(format_.get(), stream_, 0, AVSEEK_FLAG_BACKWARD); code < 0) {
            // Some files cannot seek by timestamp; fall back to the first byte.
            if (const int byte_code = avformat_seek_file(format_.get(), -1, 0, 0, 0, AVSEEK_FLAG_BYTE); byte_code < 0) {
                return ffmpeg_error("rewind", code);
            }
        }
        avcodec_flush_buffers(codec_.get());
        // Drop samples the resampler still holds.
        if (const int code = swr_init(resampler_.get()); code < 0) return ffmpeg_error("resampler", code);
        pending_.clear();
        offset_ = 0;
        draining_ = false;
        finished_ = false;
        return {};
    }

private:
    // Decodes the next frame into pending_; false at the end of the track.
    Result<bool> decode_more() {
        pending_.clear();
        offset_ = 0;
        while (!finished_) {
            const int received = avcodec_receive_frame(codec_.get(), frame_.get());
            if (received == 0) {
                auto converted = convert();
                av_frame_unref(frame_.get());
                if (!converted) return std::unexpected(converted.error());
                if (!pending_.empty()) return true;
                continue;
            }
            if (received == AVERROR_EOF) {
                finished_ = true;
                break;
            }
            if (received != AVERROR(EAGAIN)) return ffmpeg_error("decode", received);
            if (draining_) {
                finished_ = true;
                break;
            }
            const int read = av_read_frame(format_.get(), packet_.get());
            if (read == AVERROR_EOF) {
                draining_ = true;
                if (const int code = avcodec_send_packet(codec_.get(), nullptr); code < 0 && code != AVERROR_EOF) {
                    return ffmpeg_error("decode", code);
                }
                continue;
            }
            if (read < 0) return ffmpeg_error("read", read);
            if (packet_->stream_index == stream_) {
                const int sent = avcodec_send_packet(codec_.get(), packet_.get());
                av_packet_unref(packet_.get());
                // A damaged packet is skipped like DirectShow would; other errors stop the track.
                if (sent < 0 && sent != AVERROR_INVALIDDATA) return ffmpeg_error("decode", sent);
            } else {
                av_packet_unref(packet_.get());
            }
        }
        // Samples still buffered in the resampler.
        const int flushed = flush_resampler();
        if (flushed < 0) return ffmpeg_error("resampler", flushed);
        return !pending_.empty();
    }
    Result<void> convert() {
        const auto capacity = swr_get_out_samples(resampler_.get(), frame_->nb_samples);
        if (capacity < 0) return ffmpeg_error("resampler", capacity);
        pending_.resize(static_cast<std::size_t>(capacity) * 2);
        auto* target = reinterpret_cast<std::uint8_t*>(pending_.data());
        // FFmpeg 6 takes `const uint8_t**` input and FFmpeg 7 `const uint8_t* const*`; this
        // const-adding cast satisfies both.
        const auto source = const_cast<const std::uint8_t**>(frame_->extended_data);
        const int count = swr_convert(resampler_.get(), &target, capacity, source, frame_->nb_samples);
        if (count < 0) return ffmpeg_error("resampler", count);
        pending_.resize(static_cast<std::size_t>(count) * 2);
        return {};
    }
    int flush_resampler() {
        const auto capacity = swr_get_out_samples(resampler_.get(), 0);
        if (capacity <= 0) return capacity;
        pending_.resize(static_cast<std::size_t>(capacity) * 2);
        auto* target = reinterpret_cast<std::uint8_t*>(pending_.data());
        const int count = swr_convert(resampler_.get(), &target, capacity, nullptr, 0);
        pending_.resize(count > 0 ? static_cast<std::size_t>(count) * 2 : 0);
        return count;
    }

    std::unique_ptr<MemoryFile> file_;
    std::unique_ptr<AVIOContext, FreeIo> io_;
    std::unique_ptr<AVFormatContext, CloseFormat> format_;
    std::unique_ptr<AVCodecContext, FreeCodec> codec_;
    std::unique_ptr<SwrContext, FreeResampler> resampler_;
    std::unique_ptr<AVPacket, FreePacket> packet_;
    std::unique_ptr<AVFrame, FreeFrame> frame_;
    int stream_ = -1;
    std::vector<std::int16_t> pending_;
    std::size_t offset_ = 0;
    bool draining_ = false;
    bool finished_ = false;
};

class FfmpegDecoder final : public MusicDecoder {
public:
    Result<std::unique_ptr<MusicStream>> open(Bytes data) const override {
        if (data.size() > music_limit) return fail(ErrorCode::limit, "music file exceeds 64 MiB");
        auto stream = std::make_unique<FfmpegStream>(std::move(data));
        auto opened = stream->open();
        if (!opened) return std::unexpected(opened.error());
        return std::unique_ptr<MusicStream>(std::move(stream));
    }
};
}

std::unique_ptr<MusicDecoder> make_music_decoder() {
    av_log_set_level(AV_LOG_ERROR);
    return std::make_unique<FfmpegDecoder>();
}
}
