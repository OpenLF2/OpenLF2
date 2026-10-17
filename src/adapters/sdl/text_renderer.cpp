// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/detail/sdl/text_renderer.hpp"
#include <SDL3_ttf/SDL_ttf.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace openlf2 {
namespace {
// Bundled with the base package: the first covers Latin, Greek and Cyrillic, the second is the
// fallback for the CJK glyphs the first lacks.
constexpr std::string_view latin_font = "mods/base/fonts/dejavu_sans.ttf";
constexpr std::string_view fallback_font = "mods/base/fonts/droid_sans_fallback.ttf";
// Bounded like the sprite cache: when full, every texture is dropped and rendering starts over.
constexpr std::size_t max_cached_texts = 512;

struct CloseFont { void operator()(TTF_Font* font) const noexcept { TTF_CloseFont(font); } };
struct DestroyTexture { void operator()(SDL_Texture* texture) const noexcept { SDL_DestroyTexture(texture); } };
struct DestroySurface { void operator()(SDL_Surface* surface) const noexcept { SDL_DestroySurface(surface); } };
using Font = std::unique_ptr<TTF_Font, CloseFont>;
using Texture = std::unique_ptr<SDL_Texture, DestroyTexture>;
using Surface = std::unique_ptr<SDL_Surface, DestroySurface>;

std::unexpected<Error> sdl_error() { return fail(ErrorCode::platform, SDL_GetError()); }
SDL_Color rgb(int value) {
    return {static_cast<Uint8>(value >> 16), static_cast<Uint8>(value >> 8), static_cast<Uint8>(value), 255};
}
}

class TextRenderer::Impl {
public:
    explicit Impl(const ResourceSource& resources) : resources_(resources) {}
    ~Impl() {
        textures_.clear();
        fonts_.clear();
        if (initialized_) TTF_Quit();
    }
    void forget_textures() { textures_.clear(); }
    Result<SDL_Texture*> texture(SDL_Renderer& renderer, const TextCommand& command) {
        const auto key = cache_key(command);
        if (const auto found = textures_.find(key); found != textures_.end()) return found->second.get();
        auto surface = render(command);
        if (!surface) return std::unexpected(surface.error());
        Texture created(SDL_CreateTextureFromSurface(&renderer, surface->get()));
        if (!created || !SDL_SetTextureScaleMode(created.get(), SDL_SCALEMODE_NEAREST)) return sdl_error();
        if (textures_.size() >= max_cached_texts) textures_.clear();
        return textures_.emplace(key, std::move(created)).first->second.get();
    }

private:
    struct LoadedFont {
        Bytes bytes; // the font reads from this memory for as long as it is open
        Font font;
    };

    static std::string cache_key(const TextCommand& command) {
        return std::to_string(command.size) + (command.antialias ? 'a' : 'p') + std::to_string(command.color) + ':'
            + std::to_string(command.outline.value_or(-1)) + ':' + command.text;
    }
    Result<TTF_Font*> load(std::string_view path) {
        auto bytes = resources_.read(path);
        if (!bytes) return std::unexpected(bytes.error());
        LoadedFont loaded{std::move(*bytes), nullptr};
        SDL_IOStream* stream = SDL_IOFromConstMem(loaded.bytes.data(), loaded.bytes.size());
        if (stream == nullptr) return sdl_error();
        loaded.font.reset(TTF_OpenFontIO(stream, true, 13.0f));
        if (!loaded.font) return sdl_error();
        TTF_Font* font = loaded.font.get();
        fonts_.push_back(std::make_unique<LoadedFont>(std::move(loaded)));
        return font;
    }
    // Opens SDL_ttf and both fonts on first use.
    Result<void> prepare() {
        if (main_ != nullptr) return {};
        if (!initialized_) {
            if (!TTF_Init()) return sdl_error();
            initialized_ = true;
        }
        auto latin = load(latin_font);
        if (!latin) return std::unexpected(latin.error());
        auto fallback = load(fallback_font);
        if (!fallback) return std::unexpected(fallback.error());
        if (!TTF_AddFallbackFont(*latin, *fallback)) return sdl_error();
        main_ = *latin;
        fallback_ = *fallback;
        return {};
    }
    // Both fonts follow the requested size and outline width.
    bool configure(int size, int outline) {
        for (TTF_Font* font : {main_, fallback_}) {
            if (!TTF_SetFontSize(font, static_cast<float>(size)) || !TTF_SetFontOutline(font, outline)) return false;
        }
        return true;
    }
    // Wrapped with no width limit: line breaks only where the text has them.
    Surface draw(const TextCommand& command, int color) const {
        const auto length = command.text.size();
        return Surface(command.antialias
            ? TTF_RenderText_Blended_Wrapped(main_, command.text.c_str(), length, rgb(color), 0)
            : TTF_RenderText_Solid_Wrapped(main_, command.text.c_str(), length, rgb(color), 0));
    }
    Result<Surface> render(const TextCommand& command) {
        if (auto ready = prepare(); !ready) return std::unexpected(ready.error());
        if (command.text.empty()) return fail(ErrorCode::format, "empty text");
        if (!configure(command.size, 0)) return sdl_error();
        Surface body = draw(command, command.color);
        if (!body) return sdl_error();
        if (!command.outline) return body;
        // The outlined rendering is larger by the outline width all around: the plain text goes on top of it.
        if (!configure(command.size, 1)) return sdl_error();
        Surface edge = draw(command, *command.outline);
        if (!edge) return sdl_error();
        Surface canvas(SDL_ConvertSurface(edge.get(), SDL_PIXELFORMAT_ARGB8888));
        SDL_Rect inside{1, 1, body->w, body->h};
        if (!canvas || !SDL_BlitSurface(body.get(), nullptr, canvas.get(), &inside)) return sdl_error();
        return canvas;
    }

    const ResourceSource& resources_;
    bool initialized_ = false;
    std::vector<std::unique_ptr<LoadedFont>> fonts_;
    TTF_Font* main_ = nullptr;
    TTF_Font* fallback_ = nullptr;
    std::map<std::string, Texture, std::less<>> textures_;
};

TextRenderer::TextRenderer(const ResourceSource& resources) : impl_(std::make_unique<Impl>(resources)) {}
TextRenderer::~TextRenderer() = default;
bool TextRenderer::available() { return true; }
Result<SDL_Texture*> TextRenderer::texture(SDL_Renderer& renderer, const TextCommand& command) {
    return impl_->texture(renderer, command);
}
void TextRenderer::forget_textures() { impl_->forget_textures(); }
}
