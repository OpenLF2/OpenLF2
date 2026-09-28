// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/settings.hpp"
#include <cstddef>
#include <emscripten.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openlf2 {
namespace {
constexpr std::size_t settings_limit = 1024 * 1024; // matches src/core/settings.cpp
constexpr const char* storage_key = "openlf2-config";

// Wrapped in try/catch: `localStorage` itself can throw in locked-down/privacy-mode browsers.
// Reading is a length probe then a copy into a caller-owned buffer, to avoid depending on
// `_malloc` being exported to JS.
EM_JS(int, browser_settings_length_js, (const char* key), {
    try {
        const value = localStorage.getItem(UTF8ToString(key));
        return value === null ? -1 : lengthBytesUTF8(value);
    } catch (error) {
        return -1;
    }
});
EM_JS(void, browser_settings_copy_js, (const char* key, char* buffer, int capacity), {
    try {
        const value = localStorage.getItem(UTF8ToString(key));
        if (value !== null) stringToUTF8(value, buffer, capacity);
    } catch (error) {
        // Length and copy read the same key right after each other; if storage stops throwing
        // between the two calls (unlikely) the caller still only trusts the length it already has.
    }
});
EM_JS(int, browser_settings_save_js, (const char* key, const char* text), {
    try {
        localStorage.setItem(UTF8ToString(key), UTF8ToString(text));
        return 1;
    } catch (error) {
        return 0;
    }
});

class BrowserSettings final : public SettingsStore {
public:
    Result<std::optional<std::string>> load() const override {
        // "Not found" and "storage access failed" both come back as -1: a caller can't tell them
        // apart and falls back to defaults either way.
        const int length = browser_settings_length_js(storage_key);
        if (length < 0) return std::optional<std::string>{};
        std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
        browser_settings_copy_js(storage_key, buffer.data(), static_cast<int>(buffer.size()));
        return std::optional<std::string>(std::string(buffer.data(), static_cast<std::size_t>(length)));
    }
    Result<void> save(std::string_view text) override {
        if (text.size() > settings_limit) return fail(ErrorCode::limit, "configuration exceeds 1 MiB");
        const std::string value(text); // EM_JS needs a null-terminated buffer
        if (browser_settings_save_js(storage_key, value.c_str()) == 0) {
            return fail(ErrorCode::io, "browser storage is unavailable or full");
        }
        return {};
    }
    [[nodiscard]] std::string location() const override {
        return std::string("browser storage (localStorage key \"") + storage_key + "\")";
    }
};
}
std::unique_ptr<SettingsStore> make_browser_settings() { return std::make_unique<BrowserSettings>(); }
}
