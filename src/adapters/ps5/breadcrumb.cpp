// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Startup breadcrumbs for shell-launched PS5 titles, which have no terminal. The first
// calls happen in _start before static constructors run, so this avoids library state
// that needs construction: plain buffers, snprintf and stdio only.
#include "openlf2/ports/startup_trace.hpp"
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace {
// The kernel's notification request layout: the text starts at byte 45.
struct NotificationRequest {
    char reserved[45];
    char message[3075];
};

constexpr const char* log_path = "/data/openlf2-startup.log";
// Startup runs on one thread, so the step counter needs no synchronization.
unsigned step_number = 0;
} // namespace

extern "C" {
int sceKernelSendNotificationRequest(int, NotificationRequest*, std::size_t, int);
int sceKernelConfiguredFlexibleMemorySize(std::size_t* size);
int sceKernelAvailableFlexibleMemorySize(std::size_t* size);
std::size_t sceKernelGetDirectMemorySize();
}

namespace openlf2 {
void startup_breadcrumb(std::string_view step) noexcept {
    NotificationRequest request{};
    const int length = step.size() > 512 ? 512 : static_cast<int>(step.size());
    std::snprintf(request.message, sizeof(request.message), "OpenLF2 %u: %.*s", ++step_number, length,
                  step.data());
    // The first breadcrumb starts a fresh log; later ones append.
    if (auto* log = std::fopen(log_path, step_number == 1 ? "w" : "a")) {
        std::fprintf(log, "%s\n", request.message);
        std::fclose(log);
    }
    (void)sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void startup_log(std::string_view line) noexcept {
    if (auto* log = std::fopen(log_path, "a")) {
        std::fprintf(log, "%.*s\n", static_cast<int>(line.size()), line.data());
        std::fclose(log);
    }
}

void startup_memory_report() noexcept {
    // Read-only apart from one block the size of SDL's 1920x1080 window surface: probing the
    // heap until it fails can starve the whole console, so never allocate to exhaustion here.
    constexpr std::size_t mib = 1024 * 1024;
    std::size_t configured = 0;
    std::size_t available = 0;
    const int configured_result = sceKernelConfiguredFlexibleMemorySize(&configured);
    const int available_result = sceKernelAvailableFlexibleMemorySize(&available);
    const std::size_t direct = sceKernelGetDirectMemorySize();
    void* surface_sized = std::malloc(1920 * 1080 * 4);
    const bool surface_fits = surface_sized != nullptr;
    std::free(surface_sized);

    // Two short lines: a notification shows only about 60 characters before it is cut off.
    char text[96];
    std::snprintf(text, sizeof(text), "mem flex %zu free %zu MiB rc %x %x", configured / mib,
                  available / mib, static_cast<unsigned>(configured_result),
                  static_cast<unsigned>(available_result));
    startup_breadcrumb(text);
    std::snprintf(text, sizeof(text), "mem direct %zu MiB, malloc 8MiB %s", direct / mib,
                  surface_fits ? "ok" : "FAILED");
    startup_breadcrumb(text);
}
} // namespace openlf2

// C entry for the startup runtime in app_crt.cpp.
extern "C" void openlf2_ps5_breadcrumb(const char* step) noexcept {
    openlf2::startup_breadcrumb(step);
}
