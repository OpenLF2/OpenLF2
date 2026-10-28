// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// PS5's SDL2 adapter is intentionally small: fixed-screen presentation, keyboard/controller
// input, and BMP-backed sprites. Features that need desktop dialogs or SDL3 are unavailable.
#include "openlf2/detail/ps5/audio.hpp"
#include "openlf2/detail/ps5/direct_memory.hpp"
#include "openlf2/ports/platform.hpp"
#include "openlf2/ports/startup_trace.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>

namespace openlf2 {
namespace {
struct QuitSdl { ~QuitSdl() { SDL_Quit(); } };
struct DestroyWindow { void operator()(SDL_Window* value) const { SDL_DestroyWindow(value); } };
struct DestroyRenderer { void operator()(SDL_Renderer* value) const { SDL_DestroyRenderer(value); } };
struct DestroyTexture { void operator()(SDL_Texture* value) const { SDL_DestroyTexture(value); } };
struct CloseController { void operator()(SDL_GameController* value) const { SDL_GameControllerClose(value); } };
using Texture = std::unique_ptr<SDL_Texture, DestroyTexture>;
using Controller = std::unique_ptr<SDL_GameController, CloseController>;
// The renderer draws into the window surface, which SDL_RenderPresent does not flip for
// surface renderers; hand the finished frame to the PS5 video driver explicitly.
void show_frame(SDL_Renderer* renderer, SDL_Window* window) {
    SDL_RenderPresent(renderer);
    SDL_UpdateWindowSurface(window);
}
int vk(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return 0x41 + code - SDL_SCANCODE_A;
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return 0x31 + code - SDL_SCANCODE_1;
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) return 0x70 + code - SDL_SCANCODE_F1;
    switch (code) {
    case SDL_SCANCODE_0: return 0x30;
    case SDL_SCANCODE_RETURN: return 0x0d;
    case SDL_SCANCODE_ESCAPE: return 0x1b;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    default: return 0;
    }
}
std::array<Uint8, 7> font_rows(char character) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(character)))) {
    case 'A': return {14, 17, 17, 31, 17, 17, 17}; case 'B': return {30, 17, 17, 30, 17, 17, 30};
    case 'C': return {14, 17, 16, 16, 16, 17, 14}; case 'D': return {30, 17, 17, 17, 17, 17, 30};
    case 'E': return {31, 16, 16, 30, 16, 16, 31}; case 'F': return {31, 16, 16, 30, 16, 16, 16};
    case 'G': return {14, 17, 16, 23, 17, 17, 15}; case 'H': return {17, 17, 17, 31, 17, 17, 17};
    case 'I': return {14, 4, 4, 4, 4, 4, 14}; case 'J': return {7, 2, 2, 2, 18, 18, 12};
    case 'K': return {17, 18, 20, 24, 20, 18, 17}; case 'L': return {16, 16, 16, 16, 16, 16, 31};
    case 'M': return {17, 27, 21, 21, 17, 17, 17}; case 'N': return {17, 25, 21, 19, 17, 17, 17};
    case 'O': return {14, 17, 17, 17, 17, 17, 14}; case 'P': return {30, 17, 17, 30, 16, 16, 16};
    case 'Q': return {14, 17, 17, 17, 21, 18, 13}; case 'R': return {30, 17, 17, 30, 20, 18, 17};
    case 'S': return {15, 16, 16, 14, 1, 1, 30}; case 'T': return {31, 4, 4, 4, 4, 4, 4};
    case 'U': return {17, 17, 17, 17, 17, 17, 14}; case 'V': return {17, 17, 17, 17, 17, 10, 4};
    case 'W': return {17, 17, 17, 21, 21, 21, 10}; case 'X': return {17, 17, 10, 4, 10, 17, 17};
    case 'Y': return {17, 17, 10, 4, 4, 4, 4}; case 'Z': return {31, 1, 2, 4, 8, 16, 31};
    case '0': return {14, 17, 19, 21, 25, 17, 14}; case '1': return {4, 12, 4, 4, 4, 4, 14};
    case '2': return {14, 17, 1, 2, 4, 8, 31}; case '3': return {30, 1, 1, 14, 1, 1, 30};
    case '4': return {2, 6, 10, 18, 31, 2, 2}; case '5': return {31, 16, 16, 30, 1, 1, 30};
    case '6': return {14, 16, 16, 30, 17, 17, 14}; case '7': return {31, 1, 2, 4, 8, 8, 8};
    case '8': return {14, 17, 17, 14, 17, 17, 14}; case '9': return {14, 17, 17, 15, 1, 1, 14};
    case '.': return {0, 0, 0, 0, 0, 12, 12}; case ',': return {0, 0, 0, 0, 4, 4, 8};
    case ':': return {0, 12, 12, 0, 12, 12, 0}; case ';': return {0, 12, 12, 0, 4, 4, 8};
    case '!': return {4, 4, 4, 4, 4, 0, 4}; case '?': return {14, 17, 1, 2, 4, 0, 4};
    case '-': return {0, 0, 0, 31, 0, 0, 0}; case '_': return {0, 0, 0, 0, 0, 0, 31};
    case '/': return {1, 2, 2, 4, 8, 8, 16}; case '\\': return {16, 8, 8, 4, 2, 2, 1};
    case '(': return {2, 4, 8, 8, 8, 4, 2}; case ')': return {8, 4, 2, 2, 2, 4, 8};
    case '[': return {14, 8, 8, 8, 8, 8, 14}; case ']': return {14, 2, 2, 2, 2, 2, 14};
    case '\'': return {4, 4, 8, 0, 0, 0, 0}; case '+': return {0, 4, 4, 31, 4, 4, 0};
    case '=': return {0, 0, 31, 0, 31, 0, 0}; case ' ': return {};
    default: return {31, 17, 1, 2, 4, 0, 4};
    }
}
class Ps5Setup final : public SetupDialog {
public:
    Ps5Setup(SDL_Renderer* renderer, SDL_Window* window, const Viewport& viewport)
        : renderer_(renderer), window_(window), viewport_(viewport) {}
    Result<std::optional<std::size_t>> choose(std::string_view text, std::span<const std::string> answers) override {
        if (answers.empty()) return std::optional<std::size_t>{};
        std::size_t selected = 0;
        while (true) {
            draw(text, answers, selected, std::nullopt);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) return std::optional<std::size_t>{};
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.scancode == SDL_SCANCODE_UP || event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                        selected = selected == 0 ? answers.size() - 1 : selected - 1;
                    } else if (event.key.keysym.scancode == SDL_SCANCODE_DOWN || event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
                        selected = (selected + 1) % answers.size();
                    } else if (event.key.keysym.scancode == SDL_SCANCODE_RETURN || event.key.keysym.scancode == SDL_SCANCODE_SPACE) {
                        return std::optional{selected};
                    } else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        return std::optional<std::size_t>{};
                    }
                }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) selected = selected == 0 ? answers.size() - 1 : selected - 1;
                    else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) selected = (selected + 1) % answers.size();
                    else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) return std::optional{selected};
                    else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) return std::optional<std::size_t>{};
                }
            }
            SDL_Delay(16);
        }
    }
    Result<bool> progress(std::string_view text, double fraction, std::string_view cancel) override {
        const std::array<std::string, 1> answers{std::string(cancel)};
        draw(text, answers, 0, std::clamp(fraction, 0.0, 1.0));
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                || (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B)) return false;
            if ((event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_RETURN)
                || (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)) return false;
        }
        return true;
    }
    Result<std::optional<std::filesystem::path>> browse_file(const std::filesystem::path& start) override {
        struct Entry { std::filesystem::path path; std::string label; bool directory; };
        std::error_code filesystem_error;
        std::filesystem::path current = start;
        if (!std::filesystem::is_directory(current, filesystem_error)) current = current.parent_path();
        if (current.empty()) current = "/";
        std::size_t selected = 0;
        while (true) {
            std::vector<Entry> entries;
            const auto parent = current.parent_path();
            if (!parent.empty() && parent != current) entries.push_back({parent, "..", true});
            filesystem_error.clear();
            std::filesystem::directory_iterator iterator(
                current, std::filesystem::directory_options::skip_permission_denied, filesystem_error);
            if (!filesystem_error) {
                for (const auto& item : iterator) {
                    std::error_code status_error;
                    const auto status = item.symlink_status(status_error);
                    if (status_error || std::filesystem::is_symlink(status)) continue;
                    if (std::filesystem::is_directory(status)) {
                        entries.push_back({item.path(), item.path().filename().string(), true});
                        continue;
                    }
                    if (!std::filesystem::is_regular_file(status)) continue;
                    auto extension = item.path().extension().string();
                    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (extension == ".exe") entries.push_back({item.path(), item.path().filename().string(), false});
                }
            }
            std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
                if ((left.label == "..") != (right.label == "..")) return left.label == "..";
                if (left.directory != right.directory) return left.directory;
                return left.label < right.label;
            });
            if (selected >= entries.size()) selected = entries.empty() ? 0 : entries.size() - 1;

            constexpr std::size_t visible_entries = 13;
            const std::size_t first = selected >= visible_entries ? selected - visible_entries + 1 : 0;
            std::string message = "SELECT THE LF2 INSTALLER (.EXE)\nDIRECTORY: " + current.string() + "\n\n";
            if (filesystem_error) message += "CANNOT READ THIS DIRECTORY\n\n";
            if (entries.empty()) message += "NO DIRECTORIES OR .EXE FILES HERE\n";
            for (std::size_t index = first; index < entries.size() && index < first + visible_entries; ++index) {
                message += index == selected ? "> " : "  ";
                if (entries[index].directory && entries[index].label != "..") message += "[DIR] ";
                message += entries[index].label;
                message += '\n';
            }
            const std::array<std::string, 2> controls{"A: OPEN / SELECT", "B: BACK / CANCEL"};
            draw(message, controls, 0, std::nullopt);

            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) return std::optional<std::filesystem::path>{};
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.scancode == SDL_SCANCODE_UP && selected > 0) --selected;
                    else if (event.key.keysym.scancode == SDL_SCANCODE_DOWN && selected + 1 < entries.size()) ++selected;
                    else if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE || event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                        if (!parent.empty() && parent != current) { current = parent; selected = 0; }
                        else return std::optional<std::filesystem::path>{};
                    } else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        return std::optional<std::filesystem::path>{};
                    } else if ((event.key.keysym.scancode == SDL_SCANCODE_RETURN || event.key.keysym.scancode == SDL_SCANCODE_SPACE)
                               && !entries.empty()) {
                        const auto& entry = entries[selected];
                        if (entry.directory) { current = entry.path; selected = 0; }
                        else return std::optional{entry.path};
                    }
                }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP && selected > 0) --selected;
                    else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && selected + 1 < entries.size()) ++selected;
                    else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        if (!parent.empty() && parent != current) { current = parent; selected = 0; }
                        else return std::optional<std::filesystem::path>{};
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A && !entries.empty()) {
                        const auto& entry = entries[selected];
                        if (entry.directory) { current = entry.path; selected = 0; }
                        else return std::optional{entry.path};
                    }
                }
            }
            SDL_Delay(16);
        }
    }
private:
    void draw_text(std::string_view text, int x, int y, int scale) {
        SDL_SetRenderDrawColor(renderer_, 235, 235, 235, 255);
        const int origin = x;
        for (const char character : text) {
            if (character == '\n') { x = origin; y += 9 * scale; continue; }
            const auto rows = font_rows(character);
            for (std::size_t row = 0; row < rows.size(); ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((rows[row] & static_cast<Uint8>(1U << (4 - column))) == 0) continue;
                    SDL_Rect pixel{x + column * scale, y + static_cast<int>(row) * scale, scale, scale};
                    SDL_RenderFillRect(renderer_, &pixel);
                }
            }
            x += 6 * scale;
        }
    }
    void draw(std::string_view text, std::span<const std::string> answers, std::size_t selected,
              std::optional<double> fraction) {
        const int width = std::max(320, viewport_.width);
        const int height = std::max(240, viewport_.height);
        const int scale = std::max(1, std::min(width / 640, height / 480));
        const int margin = 24 * scale;
        SDL_SetRenderDrawColor(renderer_, 10, 15, 28, 255);
        SDL_RenderClear(renderer_);
        SDL_Rect panel{margin, margin, width - margin * 2, height - margin * 2};
        SDL_SetRenderDrawColor(renderer_, 28, 42, 64, 255);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_Rect line{panel.x + 12 * scale, panel.y + 12 * scale, panel.w - 24 * scale, 2 * scale};
        SDL_SetRenderDrawColor(renderer_, 93, 165, 220, 255);
        SDL_RenderFillRect(renderer_, &line);
        const int text_x = panel.x + 16 * scale;
        int text_y = panel.y + 28 * scale;
        const std::size_t max_columns = static_cast<std::size_t>(std::max(1, (panel.w - 32 * scale) / (6 * scale)));
        std::string line_text;
        const auto flush = [&](std::string_view value) {
            draw_text(value, text_x, text_y, scale);
            text_y += 10 * scale;
        };
        for (std::size_t pos = 0; pos < text.size();) {
            const auto end = text.find('\n', pos);
            const auto count = (end == std::string_view::npos ? text.size() : end) - pos;
            std::string word(text.substr(pos, count));
            while (word.size() > max_columns) {
                std::size_t split = word.rfind(' ', max_columns);
                if (split == std::string::npos || split == 0) split = max_columns;
                if (!line_text.empty()) { flush(line_text); line_text.clear(); }
                flush(word.substr(0, split));
                word.erase(0, split);
                while (!word.empty() && word.front() == ' ') word.erase(word.begin());
            }
            if (!line_text.empty() && line_text.size() + word.size() + 1 > max_columns) {
                flush(line_text);
                line_text.clear();
            }
            if (!word.empty()) {
                if (!line_text.empty()) line_text.push_back(' ');
                line_text += word;
            }
            pos = end == std::string_view::npos ? text.size() : end + 1;
            if (end != std::string_view::npos && (pos == text.size() || text[pos] == '\n')) {
                flush(line_text);
                line_text.clear();
            }
        }
        if (!line_text.empty()) flush(line_text);
        const int footer_y = panel.y + panel.h - 48 * scale;
        if (fraction) {
            SDL_Rect bar{panel.x + 16 * scale, footer_y - 18 * scale, panel.w - 32 * scale, 10 * scale};
            SDL_SetRenderDrawColor(renderer_, 4, 8, 14, 255);
            SDL_RenderFillRect(renderer_, &bar);
            bar.w = static_cast<int>(static_cast<double>(bar.w) * *fraction);
            SDL_SetRenderDrawColor(renderer_, 58, 166, 116, 255);
            SDL_RenderFillRect(renderer_, &bar);
        }
        int button_x = panel.x + 16 * scale;
        for (std::size_t index = 0; index < answers.size(); ++index) {
            const int button_w = static_cast<int>(answers[index].size()) * 6 * scale + 24 * scale;
            SDL_Rect button{button_x, footer_y, button_w, 28 * scale};
            SDL_SetRenderDrawColor(renderer_, index == selected ? 70 : 43, index == selected ? 110 : 61,
                                   index == selected ? 164 : 86, 255);
            SDL_RenderFillRect(renderer_, &button);
            draw_text(answers[index], button.x + 12 * scale, button.y + 10 * scale, scale);
            button_x += button_w + 12 * scale;
        }
        show_frame(renderer_, window_);
    }
    SDL_Renderer* renderer_;
    SDL_Window* window_;
    Viewport viewport_;
};
class Ps5Platform final : public Platform {
public:
    Ps5Platform(const ResourceSource& resources, const MusicDecoder& decoder)
        : resources_(resources), decoder_(decoder) {}
    Result<void> initialize(const Viewport& viewport) {
        // Before SDL allocates anything larger; earlier small blocks still free through libc.
        SDL_SetMemoryFunctions(ps5::fallback_malloc, ps5::fallback_calloc, ps5::fallback_realloc,
                               ps5::fallback_free);
        SDL_SetMainReady();
        // No hardware renderer exists to back a texture framebuffer; use the driver's own.
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return error();
        session_ = std::make_unique<QuitSdl>();
        report_heap_after_video_init();
        viewport_ = viewport;
        window_.reset(SDL_CreateWindow("OpenLF2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1920, 1080, SDL_WINDOW_SHOWN));
        if (!window_) return error();
        int pixel_width = 0, pixel_height = 0;
        SDL_GetWindowSizeInPixels(window_.get(), &pixel_width, &pixel_height);
        startup_breadcrumb("window " + std::to_string(pixel_width) + "x" + std::to_string(pixel_height) + " px");
        // PacBrew's SDL2 has only the software renderer, and the PS5 driver presents through the
        // window framebuffer. Request that surface directly so a failure reports its own cause
        // instead of SDL_CreateRenderer's generic "Couldn't find matching render driver".
        SDL_Surface* surface = SDL_GetWindowSurface(window_.get());
        if (surface == nullptr) return fail(ErrorCode::platform, std::string("window surface: ") + SDL_GetError());
        renderer_.reset(SDL_CreateSoftwareRenderer(surface));
        if (!renderer_) return fail(ErrorCode::platform, std::string("software renderer: ") + SDL_GetError());
        SDL_RenderSetLogicalSize(renderer_.get(), std::max(1, viewport.width), std::max(1, viewport.height));
        setup_ = std::make_unique<Ps5Setup>(renderer_.get(), window_.get(), viewport);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (!is_gamepad(index)) continue;
            Controller controller(SDL_GameControllerOpen(index));
            if (controller) controllers_.emplace(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller.get())),
                                                 std::move(controller));
        }
        report_devices("at start");
        // Sound is optional: without an audio device the game runs silently.
        audio_ = ps5::Audio::open(resources_, decoder_);
        last_report_ticks_ = SDL_GetTicks64();
        return {};
    }
    InputSnapshot poll() override {
        InputSnapshot input;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) input.quit = true;
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const int key = vk(event.key.keysym.scancode);
                if (key > 0) input.key_downs.push_back(key);
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) input.pressed = true;
            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                const int index = event.cdevice.which;
                if (is_gamepad(index)) {
                    Controller controller(SDL_GameControllerOpen(index));
                    if (controller) controllers_.emplace(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller.get())),
                                                         std::move(controller));
                }
                report_devices("after connect");
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                controllers_.erase(event.cdevice.which);
                report_devices("after disconnect");
            }
        }
        int count = 0;
        const Uint8* keys = SDL_GetKeyboardState(&count);
        for (int i = 0; i < count; ++i) {
            const int key = vk(static_cast<SDL_Scancode>(i));
            if (key > 0 && key < static_cast<int>(input.keys.size())) input.keys[static_cast<std::size_t>(key)] = keys[i] != 0;
        }
        int x = 0, y = 0;
        const Uint32 mouse = SDL_GetMouseState(&x, &y);
        float logical_x = 0.0f, logical_y = 0.0f;
        SDL_RenderWindowToLogical(renderer_.get(), x, y, &logical_x, &logical_y);
        input.pointer_x = static_cast<int>(logical_x); input.pointer_y = static_cast<int>(logical_y);
        input.button = (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        for (const auto& [id, controller] : controllers_) {
            (void)id;
            const auto button = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(controller.get(), b) != 0; };
            const auto axis = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(controller.get(), a); };
            constexpr Sint16 dead_zone = 10000;
            PadState pad;
            pad.up = button(SDL_CONTROLLER_BUTTON_DPAD_UP) || axis(SDL_CONTROLLER_AXIS_LEFTY) < -dead_zone;
            pad.down = button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || axis(SDL_CONTROLLER_AXIS_LEFTY) > dead_zone;
            pad.left = button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || axis(SDL_CONTROLLER_AXIS_LEFTX) < -dead_zone;
            pad.right = button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || axis(SDL_CONTROLLER_AXIS_LEFTX) > dead_zone;
            const std::array<SDL_GameControllerButton, 10> mapped{
                SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
                SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
                SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK};
            for (std::size_t i = 0; i < mapped.size(); ++i) {
                if (!button(mapped[i])) continue;
                pad.buttons |= std::uint32_t{1} << i;
                switch (i) {
                case 0: input.gamepad.south = true; break;
                case 1: input.gamepad.east = true; break;
                case 2: input.gamepad.west = true; break;
                case 3: input.gamepad.north = true; break;
                case 6: input.gamepad.back = true; break;
                case 7: input.gamepad.start = true; break;
                default: break;
                }
            }
            input.gamepad.up = input.gamepad.up || pad.up;
            input.gamepad.down = input.gamepad.down || pad.down;
            input.gamepad.left = input.gamepad.left || pad.left;
            input.gamepad.right = input.gamepad.right || pad.right;
            if (pad.buttons != 0 || pad.up || pad.down || pad.left || pad.right) input_seen_ = true;
            if (input.pads.size() < max_pads) input.pads.push_back(pad);
        }
        input.screen = {0, 0, viewport_.width, viewport_.height};
        return input;
    }
    Result<void> present(std::span<const DrawCommand> commands, const Viewport& viewport) override {
        // Every 5 seconds, record the frame rate, whether controller input arrived, and memory use.
        ++frames_presented_;
        if (const Uint64 now = SDL_GetTicks64(); now - last_report_ticks_ >= 5000) {
            const auto frames = frames_presented_ - frames_at_report_;
            startup_log(std::to_string(frames / 5) + " fps, controller input " + (input_seen_ ? "seen" : "none") +
                        ", " + std::to_string(controllers_.size()) + " controller(s) open");
            ps5::report_heap_usage(frames_presented_);
            last_report_ticks_ = now;
            frames_at_report_ = frames_presented_;
            input_seen_ = false;
        }
        viewport_ = viewport;
        SDL_SetRenderDrawColor(renderer_.get(), static_cast<Uint8>(viewport.red), static_cast<Uint8>(viewport.green),
                              static_cast<Uint8>(viewport.blue), 255);
        if (SDL_RenderClear(renderer_.get()) != 0) return error();
        for (const auto& command : commands) {
            const auto clip = std::visit([](const auto& item) { return item.clip; }, command);
            SDL_Rect clip_rect{};
            SDL_Rect* clip_ptr = nullptr;
            if (clip) {
                clip_rect = {clip->x, clip->y, clip->width, clip->height};
                clip_ptr = &clip_rect;
            }
            if (SDL_RenderSetClipRect(renderer_.get(), clip_ptr) != 0) return error();
            if (const auto* fill = std::get_if<FillCommand>(&command)) {
                SDL_Rect rect{fill->area.x, fill->area.y, fill->area.width, fill->area.height};
                SDL_SetRenderDrawColor(renderer_.get(), static_cast<Uint8>(fill->red), static_cast<Uint8>(fill->green),
                                      static_cast<Uint8>(fill->blue), static_cast<Uint8>(fill->alpha));
                if (SDL_RenderFillRect(renderer_.get(), &rect) != 0) return error();
            } else if (const auto* sprite = std::get_if<SpriteCommand>(&command)) {
                const std::string cache_key = sprite->resource + (sprite->color_key ? "#key" : "#opaque");
                auto found = textures_.find(cache_key);
                if (found == textures_.end()) {
                    auto bytes = resources_.read(sprite->resource);
                    if (!bytes || bytes->empty()) return bytes ? fail(ErrorCode::format, "empty sprite") : Result<void>(std::unexpected(bytes.error()));
                    SDL_RWops* rw = SDL_RWFromConstMem(bytes->data(), static_cast<int>(bytes->size()));
                    if (!rw) return error();
                    SDL_Surface* surface = SDL_LoadBMP_RW(rw, 1);
                    if (!surface) return error();
                    if (sprite->color_key && SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, 0, 0, 0)) != 0) {
                        SDL_FreeSurface(surface);
                        return error();
                    }
                    Texture texture(SDL_CreateTextureFromSurface(renderer_.get(), surface));
                    SDL_FreeSurface(surface);
                    if (!texture) return error();
                    found = textures_.emplace(cache_key, std::move(texture)).first;
                }
                SDL_Rect source{sprite->source.x, sprite->source.y, sprite->source.width, sprite->source.height};
                if (source.w == 0 && source.h == 0) {
                    SDL_QueryTexture(found->second.get(), nullptr, nullptr, &source.w, &source.h);
                }
                SDL_Rect target{sprite->x + sprite->translate_x, sprite->y, source.w, source.h};
                SDL_SetTextureColorMod(found->second.get(), static_cast<Uint8>((sprite->tint >> 16) & 0xff),
                                       static_cast<Uint8>((sprite->tint >> 8) & 0xff), static_cast<Uint8>(sprite->tint & 0xff));
                const SDL_RendererFlip flip = static_cast<SDL_RendererFlip>((sprite->mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE)
                    | (sprite->flipped ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE));
                if (SDL_RenderCopyEx(renderer_.get(), found->second.get(), &source, &target, 0, nullptr, flip) != 0) return error();
            }
        }
        SDL_RenderSetClipRect(renderer_.get(), nullptr);
        show_frame(renderer_.get(), window_.get());
        return {};
    }
    void set_text_input(bool) override {}
    Result<void> set_render_filter(RenderFilter filter) override {
        filter_ = filter == RenderFilter::linear ? filter : RenderFilter::nearest;
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, filter_ == RenderFilter::linear ? "1" : "0");
        return {};
    }
    bool supports(RenderFilter filter) const override { return filter != RenderFilter::xbrz; }
    std::string renderer_name() const override { return "sdl2"; }
    bool supports_fullscreen() const override { return false; }
    Result<void> set_fullscreen(bool) override { return {}; }
    Result<void> play(std::span<const SoundCommand> sounds) override {
        if (audio_) audio_->play(sounds);
        return {};
    }
    Result<void> rumble(std::span<const RumbleCommand>) override { return {}; }
    Result<void> choose_file(const std::optional<std::filesystem::path>&, const FileDialogText&) override {
        return fail(ErrorCode::unsupported, "file dialogs are unavailable on PS5");
    }
    Result<void> music(std::span<const MusicCommand> commands) override {
        if (audio_) audio_->music(commands);
        return {};
    }
    Result<void> open_folder(const std::filesystem::path&) override { return fail(ErrorCode::unsupported, "file manager is unavailable on PS5"); }
    Result<void> open_url(const std::string&) override { return fail(ErrorCode::unsupported, "browser is unavailable on PS5"); }
    SetupDialog& setup_dialog() override { return *setup_; }
private:
    static Result<void> error() { return fail(ErrorCode::platform, SDL_GetError()); }
    // The PS5 media remote also opens as an SDL game controller. Player 1 uses the first open
    // controller, so the remote could take that slot from the DualSense; only gamepads count.
    static bool is_gamepad(int index) {
        if (!SDL_IsGameController(index)) return false;
        const char* name = SDL_JoystickNameForIndex(index);
        return name == nullptr || std::string_view(name).find("Remote") == std::string_view::npos;
    }
    // One notification listing every joystick SDL sees and whether it has a controller mapping;
    // the game only reads devices that open as SDL game controllers.
    void report_devices(std::string_view when) const {
        std::string line = "controllers " + std::string(when) + ": " + std::to_string(controllers_.size()) + " open";
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            const char* name = SDL_JoystickNameForIndex(index);
            line += std::string("; ") + (name != nullptr ? name : "unnamed") +
                    (is_gamepad(index) ? "" : SDL_IsGameController(index) ? " (skipped)" : " (no mapping)");
        }
        if (SDL_NumJoysticks() == 0) line += "; no joysticks found";
        startup_breadcrumb(line);
    }
    // Bounded check (at most 32 MiB, freed at once) of whether SDL's video setup changed what
    // malloc can provide; the window surface needs one 8 MiB block at 1920x1080.
    static void report_heap_after_video_init() {
        std::string line = "after SDL_Init malloc 8/16/32MiB:";
        for (const std::size_t mib : {std::size_t{8}, std::size_t{16}, std::size_t{32}}) {
            void* block = std::malloc(mib * 1024 * 1024);
            line += block != nullptr ? " ok" : " FAILED";
            std::free(block);
        }
        startup_breadcrumb(line);
    }
    const ResourceSource& resources_;
    const MusicDecoder& decoder_;
    std::unique_ptr<QuitSdl> session_;
    std::unique_ptr<ps5::Audio> audio_; // closed before SDL quits
    std::unique_ptr<SDL_Window, DestroyWindow> window_;
    std::unique_ptr<SDL_Renderer, DestroyRenderer> renderer_;
    std::map<std::string, Texture, std::less<>> textures_;
    std::map<SDL_JoystickID, Controller> controllers_;
    Viewport viewport_{};
    RenderFilter filter_ = RenderFilter::nearest;
    std::unique_ptr<Ps5Setup> setup_;
    unsigned long long frames_presented_ = 0;
    unsigned long long frames_at_report_ = 0;
    Uint64 last_report_ticks_ = 0;
    bool input_seen_ = false;
};
}
Result<std::unique_ptr<Platform>> make_platform(const ResourceSource& resources, const Viewport& viewport,
                                                const MusicDecoder& decoder, std::string_view,
                                                const std::optional<WindowPlacement>&) {
    auto platform = std::make_unique<Ps5Platform>(resources, decoder);
    auto initialized = platform->initialize(viewport);
    if (!initialized) return std::unexpected(initialized.error());
    return std::unique_ptr<Platform>(std::move(platform));
}
Result<std::filesystem::path> settings_directory() {
    // A shell-launched title may not reach /user/homebrew; /data is the app-visible
    // view of /user/data and stays writable when the homebrew folder is not.
    std::string failures;
    for (const std::filesystem::path directory : {"/user/homebrew/openlf2", "/data/openlf2"}) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (!ec) return directory;
        failures += (failures.empty() ? "" : "; ") + directory.string() + ": " + ec.message();
    }
    return fail(ErrorCode::io, "cannot create PS5 settings directory (" + failures + ")");
}
}
