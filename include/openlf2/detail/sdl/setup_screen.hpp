// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
// Private to the SDL adapter: the setup screens drawn in the game window.
#include "openlf2/ports/platform.hpp"
#include <functional>
#include <string>
#include <vector>
#include <SDL3/SDL.h>

namespace openlf2 {
// Draws into the platform's window/renderer (must outlive it), scaled 2x for SDL's 8x8 debug
// font. `observe` sees every event first, so the platform can keep its controllers open.
class SdlSetupScreen final : public SetupDialog {
public:
    using Observer = std::function<void(const SDL_Event&)>;
    SdlSetupScreen(SDL_Renderer& renderer, const Viewport& viewport, Observer observe);
    Result<std::optional<std::size_t>> choose(std::string_view text, std::span<const std::string> answers) override;
    Result<bool> progress(std::string_view text, double fraction, std::string_view cancel) override;

private:
    struct Button {
        std::string label;
        SDL_FRect area;
    };
    // What an event means for the screen.
    struct Reaction {
        enum class Kind { none, closed, picked } kind = Kind::none;
        std::size_t index = 0;
    };
    [[nodiscard]] std::vector<std::string> wrap(std::string_view text) const;
    [[nodiscard]] std::vector<Button> layout(std::span<const std::string> labels) const;
    Reaction react(const SDL_Event& original, const std::vector<Button>& buttons);
    Result<void> draw(std::string_view text, const std::vector<Button>& buttons, std::optional<double> fraction);

    SDL_Renderer& renderer_;
    Viewport viewport_;
    Observer observe_;
    float width_;  // in scaled pixels
    float height_;
    std::size_t focus_ = 0;
};
}
