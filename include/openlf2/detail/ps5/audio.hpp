// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include "openlf2/ports/music.hpp"
#include "openlf2/ports/platform.hpp"
#include "openlf2/ports/resources.hpp"
#include <memory>
#include <span>

namespace openlf2::ps5 {
// Sound and music for the PS5's SDL2 build. SDL2 cannot bind audio streams to a device as the
// SDL3 adapter does, so one audio callback mixes every voice and the music. Behaviour follows
// the SDL3 adapter: one voice per sound resource restarted on each play, and looping music.
class Audio {
public:
    virtual ~Audio() = default;
    // Null when no audio device opens; the game then runs silently.
    static std::unique_ptr<Audio> open(const ResourceSource& resources, const MusicDecoder& decoder);
    virtual void play(std::span<const SoundCommand> sounds) = 0;
    virtual void music(std::span<const MusicCommand> commands) = 0;
};
} // namespace openlf2::ps5
