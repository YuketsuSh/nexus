#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nexus::net::platform {
#if defined(_WIN32)
using Socket = SOCKET;
using Length = int;
inline constexpr Socket invalid = INVALID_SOCKET;
inline void close(Socket socket) { closesocket(socket); }
inline bool pending() {
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
}
inline bool nonblocking(Socket socket) {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
struct Runtime {
    bool ok;
    Runtime() { WSADATA data{}; ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
    ~Runtime() { if (ok) WSACleanup(); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};
inline int connect_ready(Socket socket) {
    fd_set write_set, errors;
    FD_ZERO(&write_set); FD_ZERO(&errors);
    FD_SET(socket, &write_set); FD_SET(socket, &errors);
    timeval timeout{};
    return select(0, nullptr, &write_set, &errors, &timeout);
}
inline int send(Socket socket, const char* data, int size) {
    return ::send(socket, data, size, 0);
}
#else
using Socket = int;
using Length = socklen_t;
inline constexpr Socket invalid = -1;
inline void close(Socket socket) { ::close(socket); }
inline bool pending() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR; }
inline bool nonblocking(Socket socket) {
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
struct Runtime { bool ok = true; };
inline int connect_ready(Socket socket) {
    pollfd descriptor{socket, POLLOUT, 0};
    return poll(&descriptor, 1, 0);
}
inline int send(Socket socket, const char* data, int size) {
    return static_cast<int>(::send(socket, data, static_cast<std::size_t>(size), MSG_NOSIGNAL));
}
#endif

struct OwnedSocket {
    Socket value = invalid;
    OwnedSocket() = default;
    explicit OwnedSocket(Socket socket) : value(socket) {}
    ~OwnedSocket() { reset(); }
    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;
    OwnedSocket(OwnedSocket&& other) noexcept : value(other.release()) {}
    OwnedSocket& operator=(OwnedSocket&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    Socket release() noexcept {
        const auto socket = value;
        value = invalid;
        return socket;
    }
    void reset(Socket socket = invalid) {
        if (value != invalid) close(value);
        value = socket;
    }
};
}
