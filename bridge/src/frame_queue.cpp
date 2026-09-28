#include "frame_queue.h"

#include <stdexcept>
#include <utility>

namespace nexus::wire {
FrameQueue::FrameQueue(std::size_t max_frames, std::size_t max_bytes)
    : max_bytes_(max_bytes) {
    if (max_frames == 0 || max_frames > 4096 || max_bytes < header_size || max_bytes > 64 * 1024 * 1024)
        throw std::invalid_argument("queue limits out of range");
    slots_.resize(max_frames);
}

QueueStatus FrameQueue::try_push(std::uint16_t type, std::string_view payload) {
    if (!valid_type(type) || payload.size() > max_payload) return QueueStatus::invalid;
    const auto cost = header_size + payload.size();
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return QueueStatus::busy;
    if (closed_) return QueueStatus::closed;
    if (count_ == slots_.size() || cost > max_bytes_ - bytes_) return QueueStatus::full;
    // Copy only the admitted bytes, never a caller's oversized retained capacity.
    slots_[(head_ + count_) % slots_.size()].emplace(Frame{type, std::string(payload)});
    ++count_;
    bytes_ += cost;
    return QueueStatus::ok;
}

PopResult FrameQueue::try_pop() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return {QueueStatus::busy, std::nullopt};
    if (count_ == 0) return {closed_ ? QueueStatus::closed : QueueStatus::empty, std::nullopt};
    auto& slot = slots_[head_];
    const auto cost = header_size + slot->payload.size();
    PopResult result{QueueStatus::ok, std::move(slot)};
    slot.reset();
    head_ = (head_ + 1) % slots_.size();
    --count_;
    bytes_ -= cost;
    return result;
}

void FrameQueue::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
}
}
