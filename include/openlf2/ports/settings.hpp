// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace openlf2 {
// The user configuration document (JSON text). It is never part of the installer or the
// repository.
class SettingsStore {
public:
    virtual ~SettingsStore() = default;
    // The stored text, or nothing when no configuration has been saved yet.
    virtual Result<std::optional<std::string>> load() const = 0;
    // Replaces the stored text; a failed save leaves the previous text intact.
    virtual Result<void> save(std::string_view text) = 0;
    // Where the configuration lives, for messages.
    [[nodiscard]] virtual std::string location() const = 0;
};
// config.json in `directory`, created on the first save; saves go through a temporary file
// that replaces the old one.
std::unique_ptr<SettingsStore> make_file_settings(std::filesystem::path directory);
// A store that starts empty and keeps saves in memory (headless runs and tests).
std::unique_ptr<SettingsStore> make_memory_settings();
// The browser's localStorage (Emscripten only; declared unconditionally to avoid #ifdef at call sites).
std::unique_ptr<SettingsStore> make_browser_settings();

// A directory the game writes files into (recordings), by plain file name.
class FileDirectory {
public:
    virtual ~FileDirectory() = default;
    // Replaces the file; a failed write leaves an earlier file intact.
    virtual Result<void> write(std::string_view name, std::span<const char> bytes) = 0;
    // The directory on disk, if any (the replay dialog starts there).
    [[nodiscard]] virtual std::optional<std::filesystem::path> path() const = 0;
    // Names written so far in this session (for headless summaries).
    [[nodiscard]] virtual std::vector<std::string> written() const = 0;
};
std::unique_ptr<FileDirectory> make_file_directory(std::filesystem::path directory);
std::unique_ptr<FileDirectory> make_memory_directory();
}
