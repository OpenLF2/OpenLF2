// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// libc++'s random_device references getentropy(), which the public Prospero import
// stubs do not expose. Supply the POSIX contract from the system random device.
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <unistd.h>

extern "C" int getentropy(void* buffer, std::size_t length) {
    if (length > 256) {
        errno = EIO;
        return -1;
    }
    if (buffer == nullptr && length != 0) {
        errno = EFAULT;
        return -1;
    }
    const int device = open("/dev/urandom", O_RDONLY);
    if (device < 0) return -1;
    auto* output = static_cast<unsigned char*>(buffer);
    std::size_t remaining = length;
    while (remaining != 0) {
        const auto count = read(device, output, remaining);
        if (count < 0) {
            if (errno == EINTR) continue;
            const int failure = errno;
            close(device);
            errno = failure;
            return -1;
        }
        if (count == 0) {
            close(device);
            errno = EIO;
            return -1;
        }
        const auto received = static_cast<std::size_t>(count);
        output += received;
        remaining -= received;
    }
    close(device);
    return 0;
}
