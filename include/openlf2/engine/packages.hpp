// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace openlf2 {
struct ScriptModule { std::string operation; std::string target; std::string name; std::string source; };
struct PackageAsset { std::string resource; std::filesystem::path root; std::string file; };
// The host window's logical size (`window=WIDTHxHEIGHT` in a manifest). The window opens with
// it before any script runs, e.g. for the installer setup screens.
struct WindowSize { int width = 0; int height = 0; };
struct ScriptBundle {
    std::vector<ScriptModule> modules;
    WindowSize window;
    std::vector<PackageAsset> assets;
};
// Base must declare the window size; another package may change it, but two packages other
// than base must not declare different sizes.
Result<ScriptBundle> load_packages(std::span<const std::filesystem::path> roots);
std::string lua_quote(std::string_view text);
std::string bundle_source(std::span<const ScriptModule> modules);
}
