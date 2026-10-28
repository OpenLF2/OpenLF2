// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// The PacBrew Zstandard archive leaves optional trace hooks weakly unresolved.
// OpenLF2 does not enable Zstandard tracing, so return the documented disabled
// context and discard any end notification instead of importing unavailable APIs.
#include <cstdint>

extern "C" unsigned long long ZSTD_trace_decompress_begin(const void*) {
    return 0;
}

extern "C" void ZSTD_trace_decompress_end(unsigned long long, const void*) {}
