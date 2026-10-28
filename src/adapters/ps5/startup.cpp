// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/detail/ps5/startup.hpp"
#include "openlf2/ports/log.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace openlf2::ps5 {
void begin_startup_log() {
    const std::array log_paths{
        std::filesystem::path("/data/openlf2.log"),
        std::filesystem::path("/user/homebrew/openlf2/openlf2.log")
    };
    for (const auto& log_path : log_paths) {
        std::error_code log_error;
        std::filesystem::create_directories(log_path.parent_path(), log_error);
        if (log_error || std::freopen(log_path.c_str(), "w", stdout) == nullptr) continue;
        std::freopen(log_path.c_str(), "a", stderr);
        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);
        log(LogLevel::debug) << "process entered main(); stdout and stderr go to " + log_path.string();
        break;
    }
    log(LogLevel::info) << "main() reached";
    report_memory();
}
} // namespace openlf2::ps5
