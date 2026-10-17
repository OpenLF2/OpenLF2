// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Builds without SDL3_ttf: scripts see supports_text() false and keep the bitmap font.
#include "openlf2/detail/sdl/text_renderer.hpp"

namespace openlf2 {
class TextRenderer::Impl {};
TextRenderer::TextRenderer(const ResourceSource&) {}
TextRenderer::~TextRenderer() = default;
bool TextRenderer::available() { return false; }
Result<SDL_Texture*> TextRenderer::texture(SDL_Renderer&, const TextCommand&) {
    return fail(ErrorCode::unsupported, "this build has no text rendering (SDL3_ttf)");
}
}
