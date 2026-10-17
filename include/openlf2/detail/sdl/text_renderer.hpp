// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
// Private to the SDL adapter: draws TextCommand text with the TrueType fonts of the base package
// (SDL3_ttf). Every distinct text is rendered once and its texture reused afterwards. Builds
// without SDL3_ttf get a stub whose available() is false.
#include "openlf2/core/result.hpp"
#include "openlf2/ports/platform.hpp"
#include "openlf2/ports/resources.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace openlf2 {
class TextRenderer {
public:
    explicit TextRenderer(const ResourceSource& resources);
    ~TextRenderer();
    // Whether this build can render text at all.
    [[nodiscard]] static bool available();
    // The texture of `command`'s text with its size, colors and antialiasing. It belongs to the
    // renderer's cache and stays valid until the next call.
    Result<SDL_Texture*> texture(SDL_Renderer& renderer, const TextCommand& command);
    // Drops the cached textures (the renderer lost them); they are rendered again when asked for.
    void forget_textures();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
