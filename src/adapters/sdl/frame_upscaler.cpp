// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/detail/sdl/frame_upscaler.hpp"
#include <cstring>

namespace openlf2 {
std::unique_ptr<FrameUpscaler> make_frame_upscaler(SDL_Renderer& renderer) {
    const char* name = SDL_GetRendererName(&renderer);
    if (name == nullptr) return nullptr;
#ifndef __EMSCRIPTEN__
    // The SDL_GPU render-state APIs gpu_upscaler.cpp uses are newer than the SDL3 build
    // Emscripten bundles (see OPENLF2_DEPENDENCY_PROVIDER "web" in CMakeLists.txt); create_renderer
    // never selects the "gpu" backend there either, so this would never run on web regardless.
    if (std::strcmp(name, "gpu") == 0) return make_gpu_upscaler(renderer);
#endif
    return make_gl_upscaler(renderer);
}

float presentation_scale(SDL_Renderer& renderer, const SDL_Texture& source) {
    SDL_FRect area{};
    if (source.w <= 0 || !SDL_GetRenderLogicalPresentationRect(&renderer, &area) || area.w <= 0.0f) return 1.0f;
    return area.w / static_cast<float>(source.w);
}
}
