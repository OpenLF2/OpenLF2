// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
// Private to the SDL adapter: presents a frame through the xBRZ shader. SDL can't run custom
// shaders on every renderer, so there's one implementation each for GPU (SPIR-V) and GL (GLSL),
// both from the generated xbrz_shader.hpp.
#include <SDL3/SDL.h>
#include <memory>

namespace openlf2 {
class FrameUpscaler {
public:
    virtual ~FrameUpscaler() = default;
    // Draws `source` into the window's logical presentation area, or an explicit destination
    // in window pixels with logical presentation disabled. `source` must use nearest filtering
    // and stay valid for the call; only its color is used. Returns false with SDL_GetError()
    // set (or a message) when nothing could be drawn.
    virtual bool draw(SDL_Renderer& renderer, SDL_Texture& source,
                      const SDL_FRect* destination = nullptr) = 0;
};
// Each returns nothing when the renderer or the device cannot run the shader.
std::unique_ptr<FrameUpscaler> make_gpu_upscaler(SDL_Renderer& renderer);
std::unique_ptr<FrameUpscaler> make_gl_upscaler(SDL_Renderer& renderer);
// Chooses by the renderer's name ("gpu", "opengl", "opengles2").
std::unique_ptr<FrameUpscaler> make_frame_upscaler(SDL_Renderer& renderer);
// Output pixels per source pixel of the letterboxed presentation of `source`.
float presentation_scale(SDL_Renderer& renderer, const SDL_Texture& source);
}
