// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include <filesystem>
#include <span>
#include <string>

namespace openlf2 {
int run_application(std::span<const std::string> arguments, const std::filesystem::path& program_directory);
}
