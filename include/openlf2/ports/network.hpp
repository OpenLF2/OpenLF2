// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace openlf2 {
enum class NetworkStatus { idle, listening, connecting, connected };
// One peer, main-thread use. All operations are nonblocking; poll progresses connection and
// queued writes and returns currently available bytes. TCP has no message boundaries.
class NetworkTransport {
public:
    virtual ~NetworkTransport() = default;
    // Numeric IPv4 address; port 0 is allowed for an ephemeral listener (local tools/tests).
    virtual Result<void> listen(std::string_view address, std::uint16_t port) = 0;
    virtual Result<void> connect(std::string_view address, std::uint16_t port) = 0;
    virtual Result<void> send(std::span<const char> bytes) = 0;
    virtual Result<Bytes> poll() = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual NetworkStatus status() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t local_port() const noexcept = 0;
};
Result<std::unique_ptr<NetworkTransport>> make_network_transport();
}
