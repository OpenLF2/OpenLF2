// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include <cstddef>

namespace openlf2::ps5 {
// The libc heap of a shell-launched title stops growing after a few tens of MiB (it refused a
// 470 KiB block with 390 MiB of flexible memory free), so global operator new and SDL use this
// heap backed by direct memory instead. fallback_free and fallback_realloc also accept blocks
// that libc allocated before SDL switched to these functions.
void* fallback_malloc(std::size_t size) noexcept;
void* fallback_calloc(std::size_t count, std::size_t size) noexcept;
void* fallback_realloc(void* address, std::size_t size) noexcept;
void fallback_free(void* address) noexcept;
// Appends this heap's direct memory use and the free flexible memory to the startup log.
void report_heap_usage(unsigned long long frame) noexcept;
} // namespace openlf2::ps5
