// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/ports/platform.hpp"
#include <memory>

struct SDL_Renderer;
struct SDL_Texture;
namespace openlf2 {
// All lighting and silhouette projection is computed on the CPU. SDL only copies the
// resulting textures. The bounded cache owns textures belonging to one renderer.
class SpriteEffects {
public:
    SpriteEffects();
    ~SpriteEffects();
    void clear();
    Result<void> prepare(SDL_Renderer& renderer, const ResourceSource& resources,
                         std::span<const DrawCommand> commands, Rectangle area);
    Result<void> draw_shadows(SDL_Renderer& renderer, float offset_x, float offset_y);
    Result<SDL_Texture*> shaded(SDL_Renderer& renderer, const ResourceSource& resources,
                                const SpriteCommand& sprite);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
