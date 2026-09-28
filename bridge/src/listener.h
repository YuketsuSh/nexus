#pragma once

#include "transport.h"
#include <array>
#include <chrono>
#include <memory>

namespace nexus::net {
struct AcceptResult {
    wire::QueueStatus status;
    std::unique_ptr<Transport> transport;
};

// Lua-owner operations are serialized. The worker never sees Lua objects.
// Returned transports are independently owned and survive listener close.
class Listener {
public:
    static constexpr std::size_t pending_limit = 8;
    Listener(std::string address, std::uint16_t port);
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    AcceptResult accept();
    Snapshot snapshot() const noexcept;
    void close();
private:
    using Clock = std::chrono::steady_clock;
    struct Pending {
        platform::OwnedSocket socket;
        Clock::time_point expires;
    };
    void run(const std::string& address, std::uint16_t port) noexcept;
    Reason serve(const std::string& address, std::uint16_t port);
    platform::Runtime runtime_;
    std::array<Pending, pending_limit> pending_{};
    std::size_t head_ = 0, count_ = 0;
    std::mutex mutex_;
    std::mutex wake_mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::starting};
    std::atomic<Reason> reason_{Reason::none};
    std::atomic<std::uint16_t> port_{0};
    std::thread worker_;
};
}
