// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/log.hpp"
#include <iostream>

namespace openlf2 {
// std::cerr rather than stderr: the Windows GUI build redirects the stream, not the C file.
void log_write(LogLevel level, std::string_view text) noexcept {
    if (level == LogLevel::debug) return;
    try {
        std::cerr << text << '\n';
    } catch (...) {
    }
}
} // namespace openlf2
