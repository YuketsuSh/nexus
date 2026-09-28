#include "transport.h"
#include "socket_platform.h"

#include <chrono>
#include <utility>

namespace nexus::net {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto deadline = std::chrono::seconds(10);
}

const char* name(State value) noexcept {
    switch (value) {
    case State::starting: return "starting";
    case State::listening: return "listening";
    case State::connecting: return "connecting";
    case State::connected: return "connected";
    case State::closed: return "closed";
    case State::failed: return "failed";
    }
    return "failed";
}
const char* name(Reason value) noexcept {
    switch (value) {
    case Reason::none: return "none";
    case Reason::local_close: return "local_close";
    case Reason::peer_closed: return "peer_closed";
    case Reason::address: return "invalid_address";
    case Reason::socket: return "socket_error";
    case Reason::bind: return "bind_error";
    case Reason::connect: return "connect_error";
    case Reason::io: return "io_error";
    case Reason::protocol: return "protocol_error";
    case Reason::timeout: return "timeout";
    case Reason::resource: return "resource_error";
    }
    return "resource_error";
}
const char* name(wire::QueueStatus value) noexcept {
    switch (value) {
    case wire::QueueStatus::ok: return "ok";
    case wire::QueueStatus::empty: return "empty";
    case wire::QueueStatus::full: return "full";
    case wire::QueueStatus::busy: return "busy";
    case wire::QueueStatus::closed: return "closed";
    case wire::QueueStatus::invalid: return "invalid";
    }
    return "invalid";
}

Transport::Transport(bool listen, std::string address, std::uint16_t port)
    : worker_([this, listen, address = std::move(address), port] { run(listen, address, port); }) {}
Transport::Transport(platform::OwnedSocket accepted)
    : accepted_(std::move(accepted)), worker_([this] { run(false, "", 0); }) {}
Transport::~Transport() { close(); }
void Transport::close() {
    stop_ = true;
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}
void Transport::pause() {
    std::unique_lock<std::mutex> lock(wake_mutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(2), [this] { return stop_.load(); });
}
Snapshot Transport::snapshot() const noexcept {
    const auto state = state_.load();
    return {state, reason_.load(), port_.load()};
}
wire::QueueStatus Transport::send(std::string_view payload) {
    if (state_ != State::connected || stop_) return wire::QueueStatus::closed;
    return outgoing_.try_push(wire::diagnostic, payload);
}
wire::PopResult Transport::receive() { return incoming_.try_pop(); }

void Transport::run(bool listen, const std::string& address, std::uint16_t port) noexcept {
    Reason reason;
    try { reason = session(listen, address, port); }
    catch (...) { reason = Reason::resource; }
    outgoing_.close();
    incoming_.close();
    reason_ = reason;
    state_ = (reason == Reason::local_close || reason == Reason::peer_closed) ? State::closed : State::failed;
}

Reason Transport::session(bool listen, const std::string& address, std::uint16_t port) {
    if (!runtime_.ok) return Reason::socket;
    if (accepted_.value != platform::invalid) {
        platform::OwnedSocket peer(accepted_.release());
        if (!platform::nonblocking(peer.value)) return Reason::socket;
        sockaddr_in local{};
        platform::Length length = sizeof local;
        if (getsockname(peer.value, reinterpret_cast<sockaddr*>(&local), &length) != 0) return Reason::socket;
        port_ = ntohs(local.sin_port);
        return exchange(peer.value);
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (address.find('\0') != std::string::npos || inet_pton(AF_INET, address.c_str(), &target.sin_addr) != 1)
        return Reason::address;
    platform::OwnedSocket peer(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (peer.value == platform::invalid || !platform::nonblocking(peer.value)) return Reason::socket;

    if (listen) {
        int enabled = 1;
#if defined(_WIN32)
        if (setsockopt(peer.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), sizeof enabled) != 0)
            return Reason::socket;
#else
        if (setsockopt(peer.value, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) != 0) return Reason::socket;
#endif
        if (::bind(peer.value, reinterpret_cast<sockaddr*>(&target), sizeof target) != 0 || ::listen(peer.value, 1) != 0)
            return Reason::bind;
        platform::Length length = sizeof target;
        if (getsockname(peer.value, reinterpret_cast<sockaddr*>(&target), &length) != 0) return Reason::socket;
        port_ = ntohs(target.sin_port);
        state_ = State::listening;
        while (!stop_) {
            const auto accepted = ::accept(peer.value, nullptr, nullptr);
            if (accepted != platform::invalid) {
                peer.reset(accepted); // Stop accepting before exposing the connected state.
                if (!platform::nonblocking(peer.value)) return Reason::socket;
                break;
            }
            if (!platform::pending()) return Reason::io;
            pause();
        }
    } else {
        if (port == 0) return Reason::address;
        state_ = State::connecting;
        port_ = port;
        if (::connect(peer.value, reinterpret_cast<sockaddr*>(&target), sizeof target) != 0) {
            if (!platform::pending()) return Reason::connect;
            const auto start = Clock::now();
            while (!stop_) {
                const auto ready = platform::connect_ready(peer.value);
                if (ready < 0 && !platform::pending()) return Reason::connect;
                if (ready > 0) {
                    int error = 0;
                    platform::Length length = sizeof error;
                    if (getsockopt(peer.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0)
                        return Reason::connect;
                    break;
                }
                if (Clock::now() - start > deadline) return Reason::timeout;
                pause();
            }
        }
    }
    return exchange(peer.value);
}

Reason Transport::exchange(platform::Socket socket) {
    if (stop_) return Reason::local_close;
    state_ = State::connected;
    wire::Decoder decoder;
    std::optional<wire::Frame> received;
    std::string sending;
    std::size_t sent = 0, begin = 0, end = 0;
    char buffer[4096];
    auto read_start = Clock::now(), write_start = read_start;
    bool reading = false;
    while (!stop_) {
        if (sending.empty()) {
            auto next = outgoing_.try_pop();
            if (next.frame) {
                sending = wire::encode(next.frame->type, next.frame->payload);
                sent = 0;
                write_start = Clock::now();
            }
        }
        if (!sending.empty()) {
            const auto count = platform::send(socket, sending.data() + sent, static_cast<int>(sending.size() - sent));
            if (count > 0) sent += static_cast<std::size_t>(count);
            else if (count == 0 || !platform::pending()) return Reason::io;
            if (sent == sending.size()) sending.clear();
            else if (Clock::now() - write_start > deadline) return Reason::timeout;
        }
        if (received) {
            const auto pushed = incoming_.try_push(received->type, received->payload);
            if (pushed == wire::QueueStatus::ok) { received.reset(); reading = false; }
            else if (pushed != wire::QueueStatus::full && pushed != wire::QueueStatus::busy) return Reason::resource;
        }
        if (!received) {
            if (begin == end) {
                const auto count = ::recv(socket, buffer, sizeof buffer, 0);
                if (count == 0) return decoder.finish().error == wire::DecodeError::none ? Reason::peer_closed : Reason::protocol;
                if (count < 0 && !platform::pending()) return Reason::io;
                if (count > 0) { begin = 0; end = static_cast<std::size_t>(count); }
            }
            if (begin != end) {
                if (!reading) { read_start = Clock::now(); reading = true; }
                const auto decoded = decoder.consume(std::string_view(buffer + begin, end - begin));
                begin += decoded.consumed;
                if (decoded.status == wire::DecodeStatus::invalid) return Reason::protocol;
                if (decoded.status == wire::DecodeStatus::ready) received = decoder.take();
            }
        }
        if (reading && Clock::now() - read_start > deadline) return Reason::timeout;
        pause();
    }
    return Reason::local_close;
}
}
