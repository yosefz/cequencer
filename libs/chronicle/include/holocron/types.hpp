#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace chronicle {

inline constexpr std::size_t cache_line_size = 64;
inline constexpr std::size_t default_queue_size = 64 * 1024 * 1024;  // 64 MB
inline constexpr std::size_t max_message_size = 16 * 1024 * 1024;    // 16 MB
inline constexpr std::uint32_t queue_magic = 0x4352514C;             // "CRQL"
inline constexpr std::uint32_t queue_version = 2;

// Every message slot starts on this boundary, so headers and payloads are never
// misaligned. Costs up to 7 bytes of padding per message.
inline constexpr std::size_t message_align = 8;

// Cache-line aligned to prevent false sharing between header and message data.
struct alignas(cache_line_size) FileHeader {
    std::uint32_t magic;                      // Magic number for validation
    std::uint32_t version;                    // Format version
    std::uint64_t capacity;                   // Total file size
    std::atomic<std::uint64_t> write_pos;     // Current write position (atomic for cross-process)
    std::uint8_t reserved[40];                // Pad to cache line

    static constexpr std::size_t data_offset() noexcept {
        return sizeof(FileHeader);
    }
};
static_assert(sizeof(FileHeader) == cache_line_size);

// Non-owning view into mapped memory. Valid until the file is unmapped.
class MessageView {
public:
    constexpr MessageView() noexcept = default;

    constexpr MessageView(const std::byte* data, std::size_t length,
                          std::uint32_t type_id = 0) noexcept
        : data_{data}, length_{length}, type_id_{type_id} {}

    [[nodiscard]] const std::byte* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    [[nodiscard]] std::size_t size() const noexcept { return length_; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

    // Application message type, read from the same cache line as the length.
    [[nodiscard]] std::uint32_t type_id() const noexcept { return type_id_; }

    [[nodiscard]] std::span<const std::byte> as_span() const noexcept {
        return {data_, length_};
    }

    [[nodiscard]] std::string_view as_string() const noexcept {
        return {reinterpret_cast<const char*>(data_), length_};
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

private:
    const std::byte* data_ = nullptr;
    std::size_t length_ = 0;
    std::uint32_t type_id_ = 0;
};

// Wire format: [length: 4][type_id: 4][payload: length bytes][padding to message_align].
// length == 0 means write in progress.
struct MessageHeader {
    std::uint32_t length;   // Payload length (0 = incomplete)
    std::uint32_t type_id;  // Application message type, for dispatch
};
static_assert(sizeof(MessageHeader) == 8);

// Total bytes a message occupies, header and trailing padding included.
[[nodiscard]] inline constexpr std::size_t message_slot_size(std::size_t payload_len) noexcept {
    return (sizeof(MessageHeader) + payload_len + message_align - 1) & ~(message_align - 1);
}

} // namespace chronicle