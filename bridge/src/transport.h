#pragma once

#include "frame_queue.h"
#include "socket_platform.h"

#include <atomic>
#include <condition_variable>
#include <thread>

namespace nexus::net {
enum class State { starting, listening, connecting, connected, closed, failed };
enum class Reason { none, local_close, peer_closed, address, socket, bind, connect,
                    io, protocol, timeout, resource };
const char* name(State value) noexcept;
const char* name(Reason value) noexcept;
const char* name(wire::QueueStatus value) noexcept;

struct Snapshot { State state; Reason reason; std::uint16_t port; };

// A diagnostic transport session owns one peer and one joinable I/O thread.
// listen accepts one connection then closes its listening socket. Reconnection
// requires a fresh session. The eventual Proxy acceptor is a separate milestone.
class Transport {
public:
    static constexpr std::size_t queue_frames = 64;
    static constexpr std::size_t queue_bytes = 1024 * 1024;
    Transport(bool listen, std::string address, std::uint16_t port);
    explicit Transport(platform::OwnedSocket accepted);
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    wire::QueueStatus send(std::string_view payload);
    wire::PopResult receive();
    Snapshot snapshot() const noexcept;
    void close(); // Lifecycle only: cancels and joins; never waits for a peer.
private:
    void run(bool listen, const std::string& address, std::uint16_t port) noexcept;
    Reason session(bool listen, const std::string& address, std::uint16_t port);
    Reason exchange(platform::Socket socket);
    void pause();
    // Keep Winsock alive across transfers from a listener, including before the
    // new worker starts. Declared first so it outlives all sockets and the worker.
    platform::Runtime runtime_;
    platform::OwnedSocket accepted_;
    wire::FrameQueue incoming_{queue_frames, queue_bytes};
    wire::FrameQueue outgoing_{queue_frames, queue_bytes};
    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::starting};
    std::atomic<Reason> reason_{Reason::none};
    std::atomic<std::uint16_t> port_{0};
    std::mutex wake_mutex_;
    std::condition_variable wake_;
    std::thread worker_; // Last: all state exists before the worker starts.
};
}
