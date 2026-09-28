#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nexus::wire {
inline constexpr std::size_t header_size = 12;
inline constexpr std::size_t max_payload = 64 * 1024;
inline constexpr std::uint16_t version = 1;
// Only the transport diagnostic is defined yet. Business schemas land separately.
inline constexpr std::uint16_t diagnostic = 1;

struct Frame {
    std::uint16_t type = diagnostic;
    std::string payload;
};

enum class DecodeStatus { need_more, ready, invalid };
enum class DecodeError { none, magic, version, type, length, truncated };
struct DecodeResult {
    DecodeStatus status;
    std::size_t consumed;
    DecodeError error;
};

bool valid_type(std::uint16_t type) noexcept;
std::string encode(std::uint16_t type, std::string_view payload);

// One decoder per byte stream, owned by its I/O thread. It retains at most one
// frame. A ready frame must be taken before further input is consumed.
class Decoder {
public:
    DecodeResult consume(std::string_view bytes);
    std::optional<Frame> take();
    DecodeResult finish() noexcept;
    std::size_t buffered_bytes() const noexcept;
private:
    DecodeResult result(std::size_t consumed) const noexcept;
    std::array<unsigned char, header_size> header_{};
    std::size_t header_used_ = 0;
    std::uint32_t expected_ = 0;
    Frame frame_;
    DecodeStatus status_ = DecodeStatus::need_more;
    DecodeError error_ = DecodeError::none;
};
}
