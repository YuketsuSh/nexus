#pragma once

#include "frame.h"

#include <mutex>
#include <vector>

namespace nexus::wire {
enum class QueueStatus { ok, empty, full, busy, closed, invalid };
struct PopResult {
    QueueStatus status;
    std::optional<Frame> frame;
};

// Count and wire-byte limits apply together, including to empty payloads.
// Live operations never wait for the mutex. The owner must join all users before
// destroying the queue. close() is a lifecycle operation and may wait for a lock.
class FrameQueue {
public:
    FrameQueue(std::size_t max_frames, std::size_t max_bytes);
    QueueStatus try_push(std::uint16_t type, std::string_view payload);
    PopResult try_pop();
    void close();
private:
    std::mutex mutex_;
    std::vector<std::optional<Frame>> slots_;
    const std::size_t max_bytes_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::size_t bytes_ = 0;
    bool closed_ = false;
};
}
