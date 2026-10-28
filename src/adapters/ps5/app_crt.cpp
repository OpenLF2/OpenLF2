// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include <cstdint>
#include <cstdio>

namespace {
using Destructor = void (*)();
using Initializer = void (*)();

void run_forward(Initializer* first, Initializer* last) noexcept {
    if (first == nullptr || last == nullptr) return;
    while (first != last) (*first++)();
}

void run_reverse(Initializer* first, Initializer* last) noexcept {
    if (first == nullptr || last == nullptr) return;
    while (last != first) (*--last)();
}
} // namespace

extern "C" {
void _init_env(void* process_parameters);
int atexit(Destructor callback);
int sceKernelUsleep(unsigned int microseconds);
int openlf2_main(int argc, char** argv, char** envp) __asm__("main");
void openlf2_ps5_breadcrumb(const char* step) noexcept;

extern Initializer __preinit_array_start[] __attribute__((weak));
extern Initializer __preinit_array_end[] __attribute__((weak));
extern Initializer __init_array_start[] __attribute__((weak));
extern Initializer __init_array_end[] __attribute__((weak));
extern Initializer __fini_array_start[] __attribute__((weak));
extern Initializer __fini_array_end[] __attribute__((weak));

void _init() {
    run_forward(__preinit_array_start, __preinit_array_end);
    run_forward(__init_array_start, __init_array_end);
}

void _fini() {
    run_reverse(__fini_array_start, __fini_array_end);
}

__attribute__((weak)) void catchReturnFromMain(int status) {
    (void)status;
}

[[noreturn]] __attribute__((visibility("default"))) void _start(
    void* process_parameters, Destructor loader_teardown) {
    const auto* parameters = static_cast<const std::uint8_t*>(process_parameters);
    const int argc = *reinterpret_cast<const int*>(parameters);
    auto** argv = reinterpret_cast<char**>(const_cast<std::uint8_t*>(parameters) + sizeof(std::uint64_t));

    _init_env(process_parameters);
    // The build time shows which eboot the console actually launched.
    openlf2_ps5_breadcrumb("process started, build " __DATE__ " " __TIME__);
    if (loader_teardown != nullptr) (void)atexit(loader_teardown);
    (void)atexit(_fini);
    _init();
    openlf2_ps5_breadcrumb("static constructors done");
    const int status = openlf2_main(argc, argv, nullptr);
    char exited[48];
    (void)std::snprintf(exited, sizeof(exited), "exited with status %d", status);
    openlf2_ps5_breadcrumb(exited);
    catchReturnFromMain(status);
    // exit() from a shell-launched title raises SIGSYS, so stay idle like the platform
    // template does and let the system close the title (PS button, Close Application).
    openlf2_ps5_breadcrumb("finished; close the app from the PS button menu");
    for (;;) (void)sceKernelUsleep(1000000);
}
} // extern "C"
