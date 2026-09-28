// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// SDL setup screens: a question with buttons, or a progress bar, in SDL's built-in debug font.
#include "openlf2/detail/sdl/setup_screen.hpp"
#include <algorithm>
#include <array>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace openlf2 {
namespace {
// Layout in scaled pixels (the viewport divided by `scale`).
constexpr float scale = 2.0f;
constexpr float margin = 12.0f;
constexpr float glyph = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
constexpr float line_height = glyph + 4.0f;
constexpr float button_height = 20.0f;
constexpr float bar_height = 12.0f;

Error sdl_error() { return Error{ErrorCode::platform, std::string("setup screen: ") + SDL_GetError()}; }
}

SdlSetupScreen::SdlSetupScreen(SDL_Renderer& renderer, const Viewport& viewport, Observer observe)
    : renderer_(renderer), viewport_(viewport), observe_(std::move(observe)), width_(static_cast<float>(viewport.width) / scale),
      height_(static_cast<float>(viewport.height) / scale) {}

Result<std::optional<std::size_t>> SdlSetupScreen::choose(std::string_view text, std::span<const std::string> answers) {
    if (answers.empty()) return fail(ErrorCode::platform, "setup screen: a question needs an answer");
    const auto buttons = layout(answers);
    focus_ = 0;
    SDL_ShowCursor();
    while (true) {
        auto drawn = draw(text, buttons, std::nullopt);
        if (!drawn) return std::unexpected(drawn.error());
        SDL_Event event;
#ifdef __EMSCRIPTEN__
        if (!SDL_PollEvent(&event)) {
            emscripten_sleep(16);
            continue;
        }
#else
        if (!SDL_WaitEventTimeout(&event, 250)) continue;
#endif
        do {
            const auto reaction = react(event, buttons);
            if (reaction.kind == Reaction::Kind::closed) return std::optional<std::size_t>{};
            if (reaction.kind == Reaction::Kind::picked) return std::optional<std::size_t>{reaction.index};
        } while (SDL_PollEvent(&event));
    }
}

Result<bool> SdlSetupScreen::progress(std::string_view text, double fraction, std::string_view cancel) {
    const std::array labels{std::string(cancel)};
    const auto buttons = layout(labels);
    focus_ = 0;
    SDL_ShowCursor();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (react(event, buttons).kind != Reaction::Kind::none) return false;
    }
    auto drawn = draw(text, buttons, std::clamp(fraction, 0.0, 1.0));
    if (!drawn) return std::unexpected(drawn.error());
    return true;
}

// Lines that fit the width, broken between words; '\n' starts a new line and longer words
// (paths) are split.
std::vector<std::string> SdlSetupScreen::wrap(std::string_view text) const {
    const auto columns = static_cast<std::size_t>(std::max(1.0f, (width_ - 2 * margin) / glyph));
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('\n', start), text.size());
        auto paragraph = text.substr(start, end - start);
        do {
            if (paragraph.size() <= columns) {
                lines.emplace_back(paragraph);
                break;
            }
            auto cut = paragraph.substr(0, columns + 1).rfind(' ');
            if (cut == std::string_view::npos || cut == 0) cut = columns;
            lines.emplace_back(paragraph.substr(0, cut));
            paragraph = paragraph.substr(cut);
            if (!paragraph.empty() && paragraph.front() == ' ') paragraph.remove_prefix(1);
        } while (!paragraph.empty());
        start = end + 1;
    }
    return lines;
}

// Buttons in a row along the bottom, right-aligned, in the given order.
std::vector<SdlSetupScreen::Button> SdlSetupScreen::layout(std::span<const std::string> labels) const {
    std::vector<Button> buttons;
    float right = width_ - margin;
    for (auto label = labels.rbegin(); label != labels.rend(); ++label) {
        const float width = std::max(64.0f, static_cast<float>(label->size()) * glyph + 16.0f);
        buttons.insert(buttons.begin(),
                       Button{*label, SDL_FRect{right - width, height_ - margin - button_height, width, button_height}});
        right -= width + 8.0f;
    }
    return buttons;
}

SdlSetupScreen::Reaction SdlSetupScreen::react(const SDL_Event& original, const std::vector<Button>& buttons) {
    SDL_Event event = original;
    if (observe_) observe_(original);
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return {Reaction::Kind::closed};
    case SDL_EVENT_KEY_DOWN:
        switch (event.key.key) {
        case SDLK_ESCAPE: return {Reaction::Kind::closed};
        case SDLK_LEFT: focus_ = (focus_ + buttons.size() - 1) % buttons.size(); break;
        case SDLK_RIGHT: case SDLK_TAB: focus_ = (focus_ + 1) % buttons.size(); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return {Reaction::Kind::picked, focus_};
        default: break;
        }
        return {};
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: focus_ = (focus_ + buttons.size() - 1) % buttons.size(); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: focus_ = (focus_ + 1) % buttons.size(); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: return {Reaction::Kind::picked, focus_};
        case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: return {Reaction::Kind::closed};
        default: break;
        }
        return {};
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if ((event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID)
            || (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.which == SDL_TOUCH_MOUSEID)) return {};
        // The renderer's scale is back at 1 between draws, so this yields viewport coordinates.
        SDL_ConvertEventToRenderCoordinates(&renderer_, &event);
        const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
        const SDL_FPoint point{(motion ? event.motion.x : event.button.x) / scale,
                               (motion ? event.motion.y : event.button.y) / scale};
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            if (!SDL_PointInRectFloat(&point, &buttons[index].area)) continue;
            focus_ = index;
            if (!motion && event.button.button == SDL_BUTTON_LEFT) return {Reaction::Kind::picked, index};
        }
        return {};
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP: {
        // A direct touch chooses the setup button without SDL's synthetic mouse events.
        if (SDL_GetTouchDeviceType(event.tfinger.touchID) != SDL_TOUCH_DEVICE_DIRECT) return {};
        int width = 0, height = 0;
        float x = 0.0f, y = 0.0f;
        if (!SDL_GetWindowSize(SDL_GetRenderWindow(&renderer_), &width, &height)
            || !SDL_RenderCoordinatesFromWindow(&renderer_, event.tfinger.x * static_cast<float>(width),
                                                event.tfinger.y * static_cast<float>(height), &x, &y)) return {};
        const SDL_FPoint point{x / scale, y / scale};
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            if (!SDL_PointInRectFloat(&point, &buttons[index].area)) continue;
            focus_ = index;
            if (event.type == SDL_EVENT_FINGER_UP) return {Reaction::Kind::picked, index};
        }
        return {};
    }
    default:
        return {};
    }
}

Result<void> SdlSetupScreen::draw(std::string_view text, const std::vector<Button>& buttons, std::optional<double> fraction) {
    auto* renderer = &renderer_;
    const float button_top = height_ - margin - button_height;
    const float bar_top = button_top - bar_height - 12.0f;
    bool drawn = SDL_SetRenderLogicalPresentation(renderer, viewport_.width, viewport_.height, SDL_LOGICAL_PRESENTATION_LETTERBOX)
        && SDL_SetRenderScale(renderer, 1.0f, 1.0f)
        && SDL_SetRenderDrawColor(renderer, 30, 32, 40, 255) && SDL_RenderClear(renderer)
        && SDL_SetRenderScale(renderer, scale, scale);
    auto lines = wrap(text);
    const auto capacity = static_cast<std::size_t>(std::max(1.0f, ((fraction ? bar_top : button_top) - 2 * margin) / line_height));
    if (lines.size() > capacity) {
        lines.resize(capacity);
        lines.back() = "...";
    }
    drawn = drawn && SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    for (std::size_t line = 0; line < lines.size(); ++line) {
        drawn = drawn && SDL_RenderDebugText(renderer, margin, margin + static_cast<float>(line) * line_height, lines[line].c_str());
    }
    if (fraction) {
        const SDL_FRect outline{margin, bar_top, width_ - 2 * margin, bar_height};
        const SDL_FRect filled{outline.x + 2, outline.y + 2, static_cast<float>((outline.w - 4) * *fraction), outline.h - 4};
        drawn = drawn && SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255) && SDL_RenderRect(renderer, &outline)
            && SDL_SetRenderDrawColor(renderer, 80, 140, 220, 255) && SDL_RenderFillRect(renderer, &filled);
    }
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        const auto& button = buttons[index];
        const bool focused = index == focus_;
        const float label_x = button.area.x + (button.area.w - static_cast<float>(button.label.size()) * glyph) / 2;
        const float label_y = button.area.y + (button.area.h - glyph) / 2;
        drawn = drawn && SDL_SetRenderDrawColor(renderer, focused ? 80 : 60, focused ? 110 : 62, focused ? 180 : 75, 255)
            && SDL_RenderFillRect(renderer, &button.area)
            && SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255) && SDL_RenderRect(renderer, &button.area)
            && SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255)
            && SDL_RenderDebugText(renderer, label_x, label_y, button.label.c_str());
    }
    // Game frames draw at scale 1.
    drawn = drawn && SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    if (!drawn || !SDL_RenderPresent(renderer)) return std::unexpected(sdl_error());
    return {};
}
}
