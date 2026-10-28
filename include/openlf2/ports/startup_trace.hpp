// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include <string_view>

namespace openlf2 {
// Reports a startup milestone on platforms launched without a visible console. PS5 shows
// each one as a system notification and appends it to /data/openlf2-startup.log; other
// platforms already print startup diagnostics, so this does nothing there.
// startup_memory_report adds one breadcrumb with the memory budgets the title received and
// how much heap and mapped memory it can actually obtain.
// startup_log appends a line to the log file only, for frequent status lines that would flood
// the notification list.
#ifdef OPENLF2_PS5
void startup_breadcrumb(std::string_view step) noexcept;
void startup_memory_report() noexcept;
void startup_log(std::string_view line) noexcept;
#else
inline void startup_breadcrumb(std::string_view) noexcept {}
inline void startup_memory_report() noexcept {}
inline void startup_log(std::string_view) noexcept {}
#endif
} // namespace openlf2
