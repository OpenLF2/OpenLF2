// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/settings.hpp"
#include "openlf2/core/files.hpp"
#include <algorithm>
#include <fstream>
#include <system_error>

namespace openlf2 {
namespace {
constexpr std::size_t settings_limit = 1024 * 1024;

class FileSettings final : public SettingsStore {
public:
    explicit FileSettings(std::filesystem::path directory) : directory_(std::move(directory)) {}
    Result<std::optional<std::string>> load() const override {
        std::error_code error;
        const auto file = directory_ / "config.json";
        if (!std::filesystem::exists(file, error)) {
            if (error) return fail(ErrorCode::io, "cannot inspect " + file.string() + ": " + error.message());
            return std::optional<std::string>{};
        }
        auto bytes = read_file(file, settings_limit);
        if (!bytes) return std::unexpected(bytes.error());
        return std::optional<std::string>(std::string(bytes->begin(), bytes->end()));
    }
    Result<void> save(std::string_view text) override {
        if (text.size() > settings_limit) return fail(ErrorCode::limit, "configuration exceeds 1 MiB");
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        if (error) return fail(ErrorCode::io, "cannot create " + directory_.string() + ": " + error.message());
        const auto file = directory_ / "config.json";
        const auto temporary = directory_ / "config.json.tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return fail(ErrorCode::io, "cannot write " + temporary.string());
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.flush();
            if (!out) return fail(ErrorCode::io, "cannot write " + temporary.string());
        }
        std::filesystem::rename(temporary, file, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail(ErrorCode::io, "cannot replace " + file.string());
        }
        return {};
    }
    [[nodiscard]] std::string location() const override { return (directory_ / "config.json").string(); }
private:
    std::filesystem::path directory_;
};

class MemorySettings final : public SettingsStore {
public:
    Result<std::optional<std::string>> load() const override { return text_; }
    Result<void> save(std::string_view text) override {
        if (text.size() > settings_limit) return fail(ErrorCode::limit, "configuration exceeds 1 MiB");
        text_ = std::string(text);
        return {};
    }
    [[nodiscard]] std::string location() const override { return "(memory)"; }
private:
    std::optional<std::string> text_;
};
}

namespace {
bool plain_name(std::string_view name) {
    if (name.empty() || name.size() > 128 || name.front() == '.') return false;
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}
class DiskDirectory final : public FileDirectory {
public:
    explicit DiskDirectory(std::filesystem::path directory) : directory_(std::move(directory)) {}
    Result<void> write(std::string_view name, std::span<const char> bytes) override {
        if (!plain_name(name)) return fail(ErrorCode::format, "invalid file name: " + std::string(name));
        auto written = replace_file(directory_, name, bytes);
        if (written) written_.emplace_back(name);
        return written;
    }
    [[nodiscard]] std::optional<std::filesystem::path> path() const override { return directory_; }
    [[nodiscard]] std::vector<std::string> written() const override { return written_; }
private:
    std::filesystem::path directory_;
    std::vector<std::string> written_;
};
class MemoryDirectory final : public FileDirectory {
public:
    Result<void> write(std::string_view name, std::span<const char> bytes) override {
        if (!plain_name(name)) return fail(ErrorCode::format, "invalid file name: " + std::string(name));
        if (bytes.size() > 64 * 1024 * 1024) return fail(ErrorCode::limit, "file exceeds 64 MiB");
        written_.emplace_back(name);
        return {};
    }
    [[nodiscard]] std::optional<std::filesystem::path> path() const override { return std::nullopt; }
    [[nodiscard]] std::vector<std::string> written() const override { return written_; }
private:
    std::vector<std::string> written_;
};
}
std::unique_ptr<FileDirectory> make_file_directory(std::filesystem::path directory) {
    return std::make_unique<DiskDirectory>(std::move(directory));
}
std::unique_ptr<FileDirectory> make_memory_directory() { return std::make_unique<MemoryDirectory>(); }

std::unique_ptr<SettingsStore> make_file_settings(std::filesystem::path directory) {
    return std::make_unique<FileSettings>(std::move(directory));
}
std::unique_ptr<SettingsStore> make_memory_settings() { return std::make_unique<MemorySettings>(); }
}
