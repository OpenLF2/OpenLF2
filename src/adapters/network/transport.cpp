// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/network.hpp"
#include <array>
#include <cerrno>
#include <string>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openlf2 {
namespace {
#ifdef _WIN32
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr auto invalid_socket = INVALID_SOCKET;
int last_error() { return WSAGetLastError(); }
bool pending(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR; }
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr auto invalid_socket = -1;
int last_error() { return errno; }
bool pending(int error) { return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS || error == EINTR; }
#endif
// A socket is an integer capability, owned by this move-only RAII value (never a native pointer).
class Socket {
public:
    explicit Socket(NativeSocket value = invalid_socket) : value_(value) {}
    ~Socket() { reset(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : value_(std::exchange(other.value_, invalid_socket)) {}
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, invalid_socket); }
        return *this;
    }
    void reset() noexcept {
        if (value_ == invalid_socket) return;
#ifdef _WIN32
        closesocket(value_);
#else
        ::close(value_);
#endif
        value_ = invalid_socket;
    }
    NativeSocket get() const { return value_; }
private:
    NativeSocket value_;
};
struct SocketSession {
#ifdef _WIN32
    bool initialized = false;
    ~SocketSession() { if (initialized) WSACleanup(); }
#endif
    Result<void> start() {
#ifdef _WIN32
        WSADATA data{};
        const auto error = WSAStartup(MAKEWORD(2, 2), &data);
        if (error != 0) return fail(ErrorCode::platform, "Winsock startup: " + std::to_string(error));
        initialized = true;
#endif
        return {};
    }
};
auto socket_error(std::string_view operation, int error = last_error()) {
    return fail(ErrorCode::io, "Network " + std::string(operation) + ": system error " + std::to_string(error));
}
Result<void> configure(const Socket& socket) {
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(socket.get(), FIONBIO, &enabled) != 0) return socket_error("nonblocking mode");
#else
    const auto flags = fcntl(socket.get(), F_GETFL, 0);
    if (flags < 0 || fcntl(socket.get(), F_SETFL, flags | O_NONBLOCK) < 0) return socket_error("nonblocking mode");
    const auto descriptor_flags = fcntl(socket.get(), F_GETFD, 0);
    if (descriptor_flags < 0 || fcntl(socket.get(), F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) return socket_error("close-on-exec");
#endif
    const int enabled_option = 1;
    if (setsockopt(socket.get(), IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&enabled_option), sizeof(enabled_option)) != 0) return socket_error("TCP_NODELAY");
#ifdef SO_NOSIGPIPE
    if (setsockopt(socket.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled_option, sizeof(enabled_option)) != 0) return socket_error("SO_NOSIGPIPE");
#endif
    return {};
}
Result<sockaddr_in> endpoint(std::string_view address, std::uint16_t port) {
    if (address.empty() || address.size() > 15 || address.find('\0') != std::string_view::npos) {
        return fail(ErrorCode::format, "Network address must be numeric IPv4");
    }
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    const std::string terminated(address);
    if (inet_pton(AF_INET, terminated.c_str(), &result.sin_addr) != 1) {
        return fail(ErrorCode::format, "Network address must be numeric IPv4");
    }
    return result;
}
class TcpTransport final : public NetworkTransport {
public:
    Result<void> initialize() { return session_.start(); }
    Result<void> listen(std::string_view address, std::uint16_t port) override {
        close();
        auto target = endpoint(address, port);
        if (!target) return std::unexpected(target.error());
        Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listener.get() == invalid_socket) return socket_error("socket");
        auto configured = configure(listener);
        if (!configured) return configured;
        const int enabled = 1;
#ifdef _WIN32
        const auto option = SO_EXCLUSIVEADDRUSE;
#else
        const auto option = SO_REUSEADDR;
#endif
        if (setsockopt(listener.get(), SOL_SOCKET, option, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0) return socket_error("bind option");
        if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&*target), sizeof(*target)) != 0) return socket_error("bind");
        if (::listen(listener.get(), 1) != 0) return socket_error("listen");
        SocketLength length = sizeof(*target);
        if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&*target), &length) != 0) return socket_error("local port");
        port_ = ntohs(target->sin_port);
        socket_ = std::move(listener);
        status_ = NetworkStatus::listening;
        return {};
    }
    Result<void> connect(std::string_view address, std::uint16_t port) override {
        close();
        if (port == 0) return fail(ErrorCode::format, "Network peer port must be nonzero");
        auto target = endpoint(address, port);
        if (!target) return std::unexpected(target.error());
        Socket connection(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (connection.get() == invalid_socket) return socket_error("socket");
        auto configured = configure(connection);
        if (!configured) return configured;
        const auto result = ::connect(connection.get(), reinterpret_cast<const sockaddr*>(&*target), sizeof(*target));
        if (result != 0 && !pending(last_error())) return socket_error("connect");
        socket_ = std::move(connection);
        status_ = result == 0 ? NetworkStatus::connected : NetworkStatus::connecting;
        return {};
    }
    Result<void> send(std::span<const char> bytes) override {
        if (status_ != NetworkStatus::connected) return fail(ErrorCode::io, "Network peer is not connected");
        if (bytes.size() > buffer_limit - outgoing_.size()) return fail(ErrorCode::limit, "Network send queue exceeds 64 KiB");
        outgoing_.insert(outgoing_.end(), bytes.begin(), bytes.end());
        return {};
    }
    Result<Bytes> poll() override {
        auto result = progress();
        if (!result) close();
        return result;
    }
    void close() noexcept override {
        socket_.reset();
        outgoing_.clear();
        status_ = NetworkStatus::idle;
        port_ = 0;
    }
    NetworkStatus status() const noexcept override { return status_; }
    std::uint16_t local_port() const noexcept override { return port_; }
private:
    Result<Bytes> progress() {
        if (status_ == NetworkStatus::idle) return Bytes{};
        if (status_ == NetworkStatus::listening) {
            Socket peer(::accept(socket_.get(), nullptr, nullptr));
            if (peer.get() == invalid_socket) {
                if (pending(last_error())) return Bytes{};
                return socket_error("accept");
            }
            auto configured = configure(peer);
            if (!configured) return std::unexpected(configured.error());
            socket_ = std::move(peer); // closes the one-peer listener
            status_ = NetworkStatus::connected;
        }
        if (status_ == NetworkStatus::connecting) {
#ifdef _WIN32
            fd_set writable{}, failed{};
            FD_ZERO(&writable); FD_ZERO(&failed);
            FD_SET(socket_.get(), &writable); FD_SET(socket_.get(), &failed);
            timeval timeout{};
            const auto ready = ::select(0, nullptr, &writable, &failed, &timeout);
#else
            pollfd descriptor{socket_.get(), POLLOUT, 0};
            const auto ready = ::poll(&descriptor, 1, 0);
#endif
            if (ready < 0) {
                if (pending(last_error())) return Bytes{};
                return socket_error("connect poll");
            }
            if (ready == 0) return Bytes{};
            int error = 0;
            SocketLength length = sizeof(error);
            if (getsockopt(socket_.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) return socket_error("connect status");
            if (error != 0) return socket_error("connect", error);
            status_ = NetworkStatus::connected;
        }
        // Bounded work per poll. Remaining data stays queued for the next call.
        if (!outgoing_.empty()) {
#ifdef MSG_NOSIGNAL
            constexpr int send_flags = MSG_NOSIGNAL;
#else
            constexpr int send_flags = 0;
#endif
#ifdef _WIN32
            const auto send_size = static_cast<int>(outgoing_.size()); // queue is bounded to 64 KiB
#else
            const auto send_size = outgoing_.size();
#endif
            const auto sent = ::send(socket_.get(), outgoing_.data(), send_size, send_flags);
            if (sent < 0) {
                if (!pending(last_error())) return socket_error("send");
            } else if (sent == 0) return fail(ErrorCode::io, "Network connection closed while sending");
            else outgoing_.erase(outgoing_.begin(), outgoing_.begin() + sent);
        }
        std::array<char, 8192> buffer{};
#ifdef _WIN32
        const auto receive_size = static_cast<int>(buffer.size());
#else
        const auto receive_size = buffer.size();
#endif
        const auto received = ::recv(socket_.get(), buffer.data(), receive_size, 0);
        if (received < 0) {
            if (pending(last_error())) return Bytes{};
            return socket_error("receive");
        }
        if (received == 0) return fail(ErrorCode::io, "Network peer disconnected");
        return Bytes(buffer.begin(), buffer.begin() + received);
    }
    static constexpr std::size_t buffer_limit = 65536;
    SocketSession session_; // destroyed after sockets
    Socket socket_;
    NetworkStatus status_ = NetworkStatus::idle;
    std::uint16_t port_ = 0;
    Bytes outgoing_;
};
}
Result<std::unique_ptr<NetworkTransport>> make_network_transport() {
    auto transport = std::make_unique<TcpTransport>();
    auto started = transport->initialize();
    if (!started) return std::unexpected(started.error());
    return transport;
}
}
