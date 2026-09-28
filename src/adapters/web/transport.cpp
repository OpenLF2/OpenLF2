// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/network.hpp"

namespace openlf2 {
namespace {
class BrowserTransport final : public NetworkTransport {
public:
    Result<void> listen(std::string_view, std::uint16_t) override { return unsupported(); }
    Result<void> connect(std::string_view, std::uint16_t) override { return unsupported(); }
    Result<void> send(std::span<const char>) override { return unsupported(); }
    Result<Bytes> poll() override { return Bytes{}; }
    void close() noexcept override {}
    NetworkStatus status() const noexcept override { return NetworkStatus::idle; }
    std::uint16_t local_port() const noexcept override { return 0; }
private:
    static Result<void> unsupported() {
        return fail(ErrorCode::platform, "Direct TCP is unavailable in browsers");
    }
};
}
Result<std::unique_ptr<NetworkTransport>> make_network_transport() {
    return std::unique_ptr<NetworkTransport>(std::make_unique<BrowserTransport>());
}
}
