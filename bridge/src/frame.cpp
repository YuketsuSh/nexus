#include "frame.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace nexus::wire {
namespace {
std::uint16_t read16(const unsigned char* bytes) noexcept {
    return static_cast<std::uint16_t>((static_cast<unsigned>(bytes[0]) << 8) | bytes[1]);
}
std::uint32_t read32(const unsigned char* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24)
        | (static_cast<std::uint32_t>(bytes[1]) << 16)
        | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
}
}

bool valid_type(std::uint16_t type) noexcept { return type == diagnostic; }

std::string encode(std::uint16_t type, std::string_view payload) {
    if (!valid_type(type)) throw std::invalid_argument("unknown frame type");
    if (payload.size() > max_payload) throw std::invalid_argument("payload too large");
    const auto length = static_cast<std::uint32_t>(payload.size());
    const unsigned char header[header_size] = {
        'N', 'X', 'P', 0, 0, static_cast<unsigned char>(version),
        static_cast<unsigned char>(type >> 8), static_cast<unsigned char>(type),
        static_cast<unsigned char>(length >> 24), static_cast<unsigned char>(length >> 16),
        static_cast<unsigned char>(length >> 8), static_cast<unsigned char>(length)
    };
    std::string bytes(reinterpret_cast<const char*>(header), header_size);
    if (!payload.empty()) bytes.append(payload.data(), payload.size());
    return bytes;
}

DecodeResult Decoder::result(std::size_t consumed) const noexcept {
    return {status_, consumed, error_};
}

DecodeResult Decoder::consume(std::string_view bytes) {
    if (status_ != DecodeStatus::need_more) return result(0);
    std::size_t used = 0;
    if (header_used_ < header_size) {
        const auto count = std::min(bytes.size(), header_size - header_used_);
        for (std::size_t i = 0; i < count; ++i)
            header_[header_used_ + i] = static_cast<unsigned char>(bytes[i]);
        header_used_ += count;
        used += count;
        bytes.remove_prefix(count);
        if (header_used_ != header_size) return result(used);
        if (header_[0] != 'N' || header_[1] != 'X' || header_[2] != 'P' || header_[3] != 0)
            error_ = DecodeError::magic;
        else if (read16(header_.data() + 4) != version) error_ = DecodeError::version;
        else if (!valid_type(read16(header_.data() + 6))) error_ = DecodeError::type;
        else if (read32(header_.data() + 8) > max_payload) error_ = DecodeError::length;
        if (error_ != DecodeError::none) {
            status_ = DecodeStatus::invalid;
            return result(used);
        }
        frame_.type = read16(header_.data() + 6);
        expected_ = read32(header_.data() + 8);
        // Validate the complete header before any attacker-controlled allocation.
        frame_.payload.reserve(expected_);
    }
    const auto count = std::min(bytes.size(), static_cast<std::size_t>(expected_) - frame_.payload.size());
    if (count != 0) frame_.payload.append(bytes.data(), count);
    used += count;
    if (frame_.payload.size() == expected_) status_ = DecodeStatus::ready;
    return result(used);
}

std::optional<Frame> Decoder::take() {
    if (status_ != DecodeStatus::ready) return std::nullopt;
    Frame frame;
    std::swap(frame, frame_);
    header_used_ = 0;
    expected_ = 0;
    status_ = DecodeStatus::need_more;
    return frame;
}

DecodeResult Decoder::finish() noexcept {
    if (status_ == DecodeStatus::need_more && header_used_ != 0) {
        status_ = DecodeStatus::invalid;
        error_ = DecodeError::truncated;
    }
    return result(0);
}

std::size_t Decoder::buffered_bytes() const noexcept {
    return header_used_ + frame_.payload.size();
}
}
