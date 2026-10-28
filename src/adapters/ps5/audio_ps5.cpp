// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// SDL2 audio for the PS5 build (see audio.hpp). The game thread decodes sounds and music and
// changes voices only while holding the device lock; the audio callback mixes into a float
// buffer sized when the device opens, so it does not allocate.
#include "openlf2/detail/ps5/audio.hpp"
#include "openlf2/ports/log.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace openlf2::ps5 {
namespace {
constexpr int channels = 2;
// About a third of a second of music stays queued ahead of the callback, as in the SDL3
// adapter; the ring holds a full second.
constexpr std::size_t music_target_samples = static_cast<std::size_t>(music_rate) * channels / 3;
constexpr std::size_t music_ring_samples = static_cast<std::size_t>(music_rate) * channels;

// Volume and pan are hundredths of a decibel (see SoundCommand).
float gain(int hundredths) {
    if (hundredths <= -10000) return 0.0f;
    return static_cast<float>(std::pow(10.0, static_cast<double>(hundredths) / 2000.0));
}

struct Voice {
    std::vector<std::int16_t> samples; // interleaved stereo at music_rate
    std::size_t position = 0;          // samples.size() while silent
    float left = 1.0f;
    float right = 1.0f;
};

class SdlAudio final : public Audio {
public:
    SdlAudio(const ResourceSource& resources, const MusicDecoder& decoder)
        : resources_(resources), decoder_(decoder), ring_(music_ring_samples), decoded_(4096) {}
    ~SdlAudio() override {
        // Closing the device stops the callback before the voices it reads are destroyed.
        if (device_ != 0) SDL_CloseAudioDevice(device_);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    bool start() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            report(std::string("audio unavailable: ") + SDL_GetError());
            return false;
        }
        SDL_AudioSpec wanted{};
        wanted.freq = music_rate;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = channels;
        wanted.samples = 1024;
        wanted.callback = &SdlAudio::callback;
        wanted.userdata = this;
        SDL_AudioSpec obtained{};
        // No allowed changes: SDL converts to whatever the PS5 output needs.
        device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
        if (device_ == 0) {
            report(std::string("audio device did not open: ") + SDL_GetError());
            return false;
        }
        mix_.resize(static_cast<std::size_t>(obtained.samples) * channels);
        SDL_PauseAudioDevice(device_, 0);
        log(LogLevel::info) << "audio open: " << obtained.freq << " Hz, " << obtained.samples << "-frame buffer";
        return true;
    }

    void play(std::span<const SoundCommand> sounds) override {
        for (const auto& sound : sounds) {
            auto voice = voices_.find(sound.resource);
            if (voice == voices_.end()) {
                auto loaded = load_voice(sound.resource);
                if (!loaded) {
                    if (reported_.insert(sound.resource).second) report("sound " + sound.resource + ": " + loaded.error().message);
                    continue;
                }
                SDL_LockAudioDevice(device_);
                voice = voices_.emplace(sound.resource, std::move(*loaded)).first;
                SDL_UnlockAudioDevice(device_);
            }
            const float volume = gain(sound.volume);
            SDL_LockAudioDevice(device_);
            voice->second.left = volume * (sound.pan > 0 ? gain(-sound.pan) : 1.0f);
            voice->second.right = volume * (sound.pan < 0 ? gain(sound.pan) : 1.0f);
            voice->second.position = 0;
            SDL_UnlockAudioDevice(device_);
        }
    }

    void music(std::span<const MusicCommand> commands) override {
        for (const auto& command : commands) {
            switch (command.kind) {
            case MusicCommand::Kind::play:
                if (command.resource == music_resource_ && track_) {
                    music_running_ = true;
                    break;
                }
                clear_music();
                track_.reset();
                music_resource_ = command.resource;
                music_running_ = true;
                if (auto opened = open_track(command.resource)) {
                    track_ = std::move(*opened);
                } else if (reported_.insert(command.resource).second) {
                    report("music " + command.resource + " cannot play: " + opened.error().message);
                }
                break;
            case MusicCommand::Kind::stop:
                music_running_ = false;
                clear_music();
                if (track_) {
                    if (auto rewound = track_->rewind(); !rewound) track_.reset();
                }
                break;
            case MusicCommand::Kind::resume:
                if (track_) music_running_ = true;
                break;
            case MusicCommand::Kind::volume:
                SDL_LockAudioDevice(device_);
                music_gain_ = gain(command.volume);
                SDL_UnlockAudioDevice(device_);
                break;
            }
        }
        feed_music();
    }

private:
    static void callback(void* userdata, Uint8* stream, int length) {
        auto* self = static_cast<SdlAudio*>(userdata);
        auto* output = reinterpret_cast<std::int16_t*>(stream);
        std::size_t remaining = static_cast<std::size_t>(length) / sizeof(std::int16_t);
        // SDL holds the device lock while the callback runs.
        while (remaining > 0) {
            const std::size_t count = std::min(remaining, self->mix_.size());
            self->mix(output, count);
            output += count;
            remaining -= count;
        }
    }
    void mix(std::int16_t* output, std::size_t count) {
        std::fill_n(mix_.begin(), count, 0.0f);
        for (auto& [name, voice] : voices_) {
            (void)name;
            const std::size_t available = voice.samples.size() - voice.position;
            const std::size_t used = std::min(count, available);
            const std::int16_t* source = voice.samples.data() + voice.position;
            for (std::size_t index = 0; index + 1 < used; index += 2) {
                mix_[index] += static_cast<float>(source[index]) * voice.left;
                mix_[index + 1] += static_cast<float>(source[index + 1]) * voice.right;
            }
            voice.position += used;
        }
        const std::size_t music_used = std::min(count, ring_count_);
        for (std::size_t index = 0; index < music_used; ++index) {
            mix_[index] += static_cast<float>(ring_[(ring_read_ + index) % ring_.size()]) * music_gain_;
        }
        ring_read_ = (ring_read_ + music_used) % ring_.size();
        ring_count_ -= music_used;
        for (std::size_t index = 0; index < count; ++index) {
            const float sample = std::clamp(mix_[index], -32768.0f, 32767.0f);
            output[index] = static_cast<std::int16_t>(std::lrint(sample));
        }
    }

    // Decodes a RIFF/PCM sound with SDL and converts it to interleaved 16-bit stereo.
    Result<Voice> load_voice(const std::string& resource) {
        auto bytes = resources_.read(resource);
        if (!bytes) return std::unexpected(bytes.error());
        if (bytes->size() > 16 * 1024 * 1024) return fail(ErrorCode::limit, "sound exceeds 16 MiB");
        SDL_RWops* input = SDL_RWFromConstMem(bytes->data(), static_cast<int>(bytes->size()));
        if (input == nullptr) return fail(ErrorCode::platform, SDL_GetError());
        SDL_AudioSpec spec{};
        Uint8* wav = nullptr;
        Uint32 wav_length = 0;
        if (SDL_LoadWAV_RW(input, 1, &spec, &wav, &wav_length) == nullptr) {
            return fail(ErrorCode::format, std::string("cannot decode: ") + SDL_GetError());
        }
        SDL_AudioCVT conversion{};
        const int built = SDL_BuildAudioCVT(&conversion, spec.format, spec.channels, spec.freq, AUDIO_S16SYS,
                                            channels, music_rate);
        if (built < 0) {
            SDL_FreeWAV(wav);
            return fail(ErrorCode::format, std::string("cannot convert: ") + SDL_GetError());
        }
        std::vector<Uint8> buffer(static_cast<std::size_t>(wav_length) * static_cast<std::size_t>(std::max(1, conversion.len_mult)));
        std::memcpy(buffer.data(), wav, wav_length);
        SDL_FreeWAV(wav);
        std::size_t converted_length = wav_length;
        if (built == 1) {
            conversion.buf = buffer.data();
            conversion.len = static_cast<int>(wav_length);
            if (SDL_ConvertAudio(&conversion) != 0) return fail(ErrorCode::format, std::string("cannot convert: ") + SDL_GetError());
            converted_length = static_cast<std::size_t>(conversion.len_cvt);
        }
        Voice voice;
        voice.samples.resize(converted_length / sizeof(std::int16_t) / channels * channels);
        std::memcpy(voice.samples.data(), buffer.data(), voice.samples.size() * sizeof(std::int16_t));
        voice.position = voice.samples.size();
        return voice;
    }
    Result<std::unique_ptr<MusicStream>> open_track(const std::string& resource) {
        auto bytes = resources_.read(resource);
        if (!bytes) return std::unexpected(bytes.error());
        return decoder_.open(std::move(*bytes));
    }
    void clear_music() {
        SDL_LockAudioDevice(device_);
        ring_count_ = 0;
        SDL_UnlockAudioDevice(device_);
    }
    // Keeps music_target_samples queued; at the end the track starts again (the original
    // rewinds on EC_COMPLETE).
    void feed_music() {
        if (!music_running_ || !track_) return;
        int empty_reads = 0;
        for (;;) {
            SDL_LockAudioDevice(device_);
            const std::size_t queued = ring_count_;
            SDL_UnlockAudioDevice(device_);
            if (queued >= music_target_samples) return;
            const std::size_t wanted = std::min(decoded_.size(), ring_.size() - queued) / channels * channels;
            auto count = track_->read(std::span(decoded_.data(), wanted));
            if (!count) {
                if (reported_.insert(music_resource_).second) report("music " + music_resource_ + " stopped: " + count.error().message);
                track_.reset();
                return;
            }
            if (*count == 0) {
                // An empty track would loop forever.
                if (++empty_reads > 1) return;
                if (auto rewound = track_->rewind(); !rewound) {
                    track_.reset();
                    return;
                }
                continue;
            }
            empty_reads = 0;
            SDL_LockAudioDevice(device_);
            const std::size_t write = (ring_read_ + ring_count_) % ring_.size();
            for (std::size_t index = 0; index < *count; ++index) ring_[(write + index) % ring_.size()] = decoded_[index];
            ring_count_ += *count;
            SDL_UnlockAudioDevice(device_);
        }
    }
    static void report(const std::string& message) {
        log(LogLevel::debug) << message;
    }

    const ResourceSource& resources_;
    const MusicDecoder& decoder_;
    SDL_AudioDeviceID device_ = 0;
    // Shared with the callback; changed only under the device lock.
    std::map<std::string, Voice, std::less<>> voices_;
    std::vector<std::int16_t> ring_;
    std::size_t ring_read_ = 0;
    std::size_t ring_count_ = 0;
    float music_gain_ = 1.0f;
    std::vector<float> mix_;
    // Game thread only.
    std::vector<std::int16_t> decoded_;
    std::unique_ptr<MusicStream> track_;
    std::string music_resource_;
    bool music_running_ = false;
    std::set<std::string, std::less<>> reported_;
};
} // namespace

std::unique_ptr<Audio> Audio::open(const ResourceSource& resources, const MusicDecoder& decoder) {
    auto audio = std::make_unique<SdlAudio>(resources, decoder);
    if (!audio->start()) return nullptr;
    return audio;
}
} // namespace openlf2::ps5
