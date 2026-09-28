// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <filesystem>
#include <span>
#include <string_view>

namespace openlf2 {
Result<Bytes> read_file(const std::filesystem::path& path, std::size_t limit);
// Writes `name` in `directory` (created if needed) through a temporary file that then
// replaces the target, so a failed write leaves an earlier file intact.
Result<void> replace_file(const std::filesystem::path& directory, std::string_view name,
                          std::span<const char> bytes);
Result<std::string> virtual_path(std::string_view input);
Result<std::filesystem::path> package_file(const std::filesystem::path& root,
                                           std::string_view relative);
}
