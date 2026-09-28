#include "listener.h"
#include <utility>

namespace nexus::net {
Listener::Listener(std::string address, std::uint16_t port)
    : worker_([this, address = std::move(address), port] { run(address, port); }) {}
Listener::~Listener() { close(); }

Snapshot Listener::snapshot() const noexcept {
    const auto state = state_.load();
    return {state, reason_.load(), port_.load()};
}

void Listener::close() {
    stop_ = true;
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
    // No worker remains; accept/close are both called by the one Lua owner.
    for (auto& entry : pending_) entry.socket.reset();
    count_ = 0;
}

AcceptResult Listener::accept() {
    platform::OwnedSocket socket;
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return {wire::QueueStatus::busy, nullptr};
        if (stop_ || state_ == State::failed || state_ == State::closed)
            return {wire::QueueStatus::closed, nullptr};
        if (count_ == 0) return {wire::QueueStatus::empty, nullptr};
        auto& next = pending_[head_];
        // Expired entries are never handed to Lua, even if the worker has not
        // yet run its expiry pass. Drop one per call to bound owner-thread work.
        const bool expired = Clock::now() >= next.expires;
        socket = std::move(next.socket);
        head_ = (head_ + 1) % pending_limit;
        --count_;
        if (expired) return {wire::QueueStatus::empty, nullptr};
    }
    // Construction (including allocation and worker creation) occurs outside the
    // pending-queue lock. Ownership is exception-safe at every transfer.
    return {wire::QueueStatus::ok, std::make_unique<Transport>(std::move(socket))};
}

void Listener::run(const std::string& address, std::uint16_t port) noexcept {
    Reason reason;
    try { reason = serve(address, port); }
    catch (...) { reason = Reason::resource; }
    // Failed/cancelled listeners do not retain unclaimed accepted sockets.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : pending_) entry.socket.reset();
        count_ = 0;
    }
    reason_ = reason;
    state_ = reason == Reason::local_close ? State::closed : State::failed;
}

Reason Listener::serve(const std::string& address, std::uint16_t port) {
    if (!runtime_.ok) return Reason::socket;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (address.find('\0') != std::string::npos || inet_pton(AF_INET, address.c_str(), &target.sin_addr) != 1)
        return Reason::address;
    platform::OwnedSocket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (socket.value == platform::invalid || !platform::nonblocking(socket.value)) return Reason::socket;
    int enabled = 1;
#if defined(_WIN32)
    if (setsockopt(socket.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), sizeof enabled) != 0)
        return Reason::socket;
#else
    if (setsockopt(socket.value, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) != 0) return Reason::socket;
#endif
    if (::bind(socket.value, reinterpret_cast<sockaddr*>(&target), sizeof target) != 0 ||
        ::listen(socket.value, static_cast<int>(pending_limit)) != 0) return Reason::bind;
    platform::Length length = sizeof target;
    if (getsockname(socket.value, reinterpret_cast<sockaddr*>(&target), &length) != 0) return Reason::socket;
    port_ = ntohs(target.sin_port);
    state_ = State::listening;
    while (!stop_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto now = Clock::now();
            while (count_ > 0 && now >= pending_[head_].expires) {
                pending_[head_].socket.reset();
                head_ = (head_ + 1) % pending_limit;
                --count_;
            }
        }
        platform::OwnedSocket accepted(::accept(socket.value, nullptr, nullptr));
        if (accepted.value != platform::invalid) {
            if (platform::nonblocking(accepted.value)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (count_ < pending_limit && !stop_) {
                    auto& entry = pending_[(head_ + count_) % pending_limit];
                    entry.socket = std::move(accepted);
                    entry.expires = Clock::now() + std::chrono::seconds(10);
                    ++count_;
                }
                // Full queues close the excess peer; no thread or Lua object
                // is created for it. Admission is at most one socket per cycle.
            }
        } else if (!platform::pending()) return Reason::io;
        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(2), [this] { return stop_.load(); });
    }
    return Reason::local_close;
}
}
