// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

namespace openlf2::ps5 {
// Shell-launched apps have no attached terminal. Redirects stdout and stderr to a log file
// and reports the first startup breadcrumbs, so a failed SDL or application initialization
// can be told apart from a loader failure.
void begin_startup_log();
// Logs the memory budgets the title received and how much heap and mapped memory it can
// actually obtain.
void report_memory() noexcept;
} // namespace openlf2::ps5
