// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/engine/packages.hpp"
#include "openlf2/core/files.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace openlf2 {
namespace {
struct Declaration { std::string operation; std::string target; std::string file; };
struct Package {
    std::filesystem::path root;
    std::string id;
    std::string version;
    std::map<std::string, std::string, std::less<>> dependencies;
    std::vector<Declaration> declarations;
    std::vector<std::pair<std::string, std::string>> assets;
    std::optional<WindowSize> window;
};
bool identifier(std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_' || character == '-' || character == '.';
    });
}
// "WIDTHxHEIGHT", each 1 to 4096.
std::optional<WindowSize> window_size(std::string_view value) {
    const auto split = value.find('x');
    if (split == std::string_view::npos) return std::nullopt;
    const auto number = [](std::string_view text) -> std::optional<int> {
        int parsed = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
        if (parsed < 1 || parsed > 4096) return std::nullopt;
        return parsed;
    };
    const auto width = number(value.substr(0, split));
    const auto height = number(value.substr(split + 1));
    if (!width || !height) return std::nullopt;
    return WindowSize{*width, *height};
}
Result<Package> parse_package(const std::filesystem::path& root) {
    auto manifest_path = package_file(root, "package.manifest");
    if (!manifest_path) return std::unexpected(manifest_path.error());
    auto bytes = read_file(*manifest_path, 64 * 1024);
    if (!bytes) return std::unexpected(bytes.error());
    Package package;
    package.root = root;
    std::istringstream stream(std::string(bytes->begin(), bytes->end()));
    std::string line;
    bool api_seen = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) return fail(ErrorCode::format, "manifest needs key=value");
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "id") {
            if (!package.id.empty() || !identifier(value)) return fail(ErrorCode::format, "invalid package id");
            package.id = value;
        } else if (key == "version") {
            if (!package.version.empty() || !identifier(value)) return fail(ErrorCode::format, "invalid version");
            package.version = value;
        } else if (key == "api") {
            if (api_seen || value != "1") return fail(ErrorCode::unsupported, "package requires API 1");
            api_seen = true;
        } else if (key == "requires") {
            const auto split = value.find('@');
            if (split == std::string::npos || !identifier(value.substr(0, split)) ||
                !identifier(value.substr(split + 1)) ||
                !package.dependencies.emplace(value.substr(0, split), value.substr(split + 1)).second) {
                return fail(ErrorCode::format, "dependency needs unique id@exact-version");
            }
        } else if (key == "asset") {
            const auto split = value.find('|');
            if (split == std::string::npos) return fail(ErrorCode::format, "asset needs resource|file");
            auto resource = virtual_path(value.substr(0, split));
            auto file = virtual_path(value.substr(split + 1));
            if (!resource || !file) return fail(ErrorCode::format, "invalid asset path");
            package.assets.emplace_back(std::move(*resource), std::move(*file));
        } else if (key == "module" || key == "replace" || key == "extend") {
            const auto split = value.find('|');
            if (split == std::string::npos) return fail(ErrorCode::format, "module needs target|file");
            auto target = virtual_path(value.substr(0, split));
            auto file = virtual_path(value.substr(split + 1));
            if (!target || !file) return fail(ErrorCode::format, "invalid module path");
            package.declarations.push_back({key, *target, *file});
        } else if (key == "window") {
            if (package.window) return fail(ErrorCode::format, "duplicate window size");
            package.window = window_size(value);
            if (!package.window) return fail(ErrorCode::format, "window needs WIDTHxHEIGHT (1-4096)");
        } else return fail(ErrorCode::format, "unrecognized manifest key: " + key);
    }
    if (package.id.empty() || package.version.empty() || !api_seen) {
        return fail(ErrorCode::format, "manifest needs id, version, api");
    }
    return package;
}
}
Result<ScriptBundle> load_packages(std::span<const std::filesystem::path> roots) {
    if (roots.size() > 64) return fail(ErrorCode::limit, "at most 64 script packages");
    std::map<std::string, Package, std::less<>> packages;
    for (const auto& root : roots) {
        auto package = parse_package(root);
        if (!package) return fail(package.error().code, root.string() + ": " + package.error().message);
        const auto id = package->id;
        if (!packages.emplace(id, std::move(*package)).second) return fail(ErrorCode::format, "duplicate package: " + id);
    }
    if (!packages.contains("base")) return fail(ErrorCode::dependency, "base package required");
    if (!packages.at("base").window) return fail(ErrorCode::format, "base package must declare its window size");
    ScriptBundle bundle{{}, *packages.at("base").window, {}};
    std::optional<std::string> window_owner;
    for (const auto& [id, package] : packages) {
        if (id == "base" || !package.window) continue;
        if (window_owner && (package.window->width != bundle.window.width || package.window->height != bundle.window.height)) {
            return fail(ErrorCode::dependency, "conflicting window sizes: " + *window_owner + " and " + id);
        }
        window_owner = id;
        bundle.window = *package.window;
    }
    for (const auto& [id, package] : packages) {
        if (id != "base" && !package.dependencies.contains("base")) {
            return fail(ErrorCode::dependency, id + " must declare base dependency");
        }
        for (const auto& [dependency, version] : package.dependencies) {
            if (!packages.contains(dependency) || packages.at(dependency).version != version) {
                return fail(ErrorCode::dependency, id + " needs " + dependency + "@" + version);
            }
        }
    }
    auto& modules = bundle.modules;
    std::set<std::string> loaded, declared, replaced, asset_paths;
    std::size_t source_bytes = 0;
    while (loaded.size() < packages.size()) {
        bool progressed = false;
        for (const auto& [id, package] : packages) {
            if (loaded.contains(id)) continue;
            if (!std::ranges::all_of(package.dependencies, [&](const auto& dependency) {
                return loaded.contains(dependency.first);
            })) continue;
            for (const auto& [resource, file] : package.assets) {
                if (!resource.starts_with("mods/" + id + "/")) {
                    return fail(ErrorCode::format, id + " asset must use mods/" + id + "/: " + resource);
                }
                if (!asset_paths.insert(resource).second) {
                    return fail(ErrorCode::format, "duplicate asset: " + resource);
                }
                auto path = package_file(package.root, file);
                if (!path) return std::unexpected(path.error());
                bundle.assets.push_back({resource, package.root, file});
                if (bundle.assets.size() > 1024) return fail(ErrorCode::limit, "too many package assets");
            }
            for (const auto& declaration : package.declarations) {
                const auto target = declaration.operation == "module" ? id + "/" + declaration.target : declaration.target;
                if (declaration.operation == "module") {
                    if (!declared.insert(target).second) return fail(ErrorCode::format, "duplicate module: " + target);
                } else {
                    const auto owner = target.substr(0, target.find('/'));
                    if (!declared.contains(target) || (owner != id && !package.dependencies.contains(owner))) {
                        return fail(ErrorCode::dependency, "override needs declared module and owner dependency: " + target);
                    }
                    if (declaration.operation == "replace" && !replaced.insert(target).second) {
                        return fail(ErrorCode::dependency, "conflicting replacements: " + target);
                    }
                }
                auto path = package_file(package.root, declaration.file);
                if (!path) return std::unexpected(path.error());
                auto source = read_file(*path, 1024 * 1024);
                if (!source) return std::unexpected(source.error());
                source_bytes += source->size();
                if (source_bytes > 8 * 1024 * 1024 || modules.size() >= 512) return fail(ErrorCode::limit, "script bundle too large");
                modules.push_back({declaration.operation, target, id + "/" + declaration.file,
                                   std::string(source->begin(), source->end())});
            }
            loaded.insert(id);
            progressed = true;
            break; // Reconsider lexicographically first ready package after each insertion.
        }
        if (!progressed) return fail(ErrorCode::dependency, "cyclic package dependencies");
    }
    return bundle;
}
std::string lua_quote(std::string_view text) {
    std::string quoted = "\"";
    for (const char byte : text) {
        const auto character = static_cast<unsigned char>(byte);
        if (character < 32 || character > 126 || character == '\\' || character == '"') {
            quoted += '\\';
            quoted += static_cast<char>('0' + character / 100);
            quoted += static_cast<char>('0' + (character / 10) % 10);
            quoted += static_cast<char>('0' + character % 10);
        } else quoted += static_cast<char>(character);
    }
    return quoted + "\"";
}
std::string bundle_source(std::span<const ScriptModule> modules) {
    std::string source = "local declarations = {\n";
    for (const auto& module : modules) {
        source += "{" + lua_quote(module.operation) + "," + lua_quote(module.target) + "," +
                  lua_quote(module.name) + "," + lua_quote(module.source) + "},\n";
    }
    return source + "}\n";
}
}
