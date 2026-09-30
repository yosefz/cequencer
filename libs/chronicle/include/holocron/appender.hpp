#pragma once

#include "holocron/types.hpp"
#include "holocron/mappedFile.hpp"
#include "zbuffer/zbuffer.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>

namespace chronicle {

// Single-writer memory-mapped queue file. Not thread-safe.
class Appender {
public:
    // size is only used when creating a new file.
    static Appender create(const std::filesystem::path& path,
                           std::size_t size = default_queue_size);

    Appender(Appender&&) noexcept = default;
    Appender& operator=(Appender&&) noexcept = default;

    // Aborts if message is empty, too large, or no space remains.
    void write(std::span<const std::byte> data, std::uint32_t type_id = 0) noexcept {
        const std::size_t len = data.size();

        if (len == 0 || len > max_message_size) [[unlikely]] {
            std::abort();
        }

        const std::size_t total = message_slot_size(len);
        const std::size_t pos = header_->write_pos.load(std::memory_order_relaxed);

        if (pos + total > capacity_) [[unlikely]] {
            std::abort();
        }

        std::byte* slot = data_start_ + pos;
        auto* msg_hdr = reinterpret_cast<MessageHeader*>(slot);
        msg_hdr->length = 0;  // incomplete marker
        msg_hdr->type_id = type_id;

        std::memcpy(slot + sizeof(MessageHeader), data.data(), len);

        // The release store below is the queue's sole synchronization edge: it
        // orders the header and payload writes above for any tailer that
        // acquire-loads write_pos, and no reader inspects a message header
        // without having done so. An extra release fence here would be redundant.
        msg_hdr->length = static_cast<std::uint32_t>(len);
        header_->write_pos.store(pos + total, std::memory_order_release);
    }

    void write(std::string_view sv, std::uint32_t type_id = 0) noexcept {
        write(std::as_bytes(std::span{sv.data(), sv.size()}), type_id);
    }

    void write(const void* data, std::size_t len, std::uint32_t type_id = 0) noexcept {
        write(std::span{static_cast<const std::byte*>(data), len}, type_id);
    }

    void write(const zbuffer::ZBuffer& buf, std::uint32_t type_id = 0) noexcept {
        write(buf.peek(), type_id);
    }

    [[nodiscard]] std::size_t available() const noexcept {
        std::size_t pos = header_->write_pos.load(std::memory_order_relaxed);
        return (pos < capacity_) ? (capacity_ - pos) : 0;
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return header_->write_pos.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return file_.path();
    }

    void sync() noexcept {
        file_.sync();
    }

private:
    Appender(MappedFile file) noexcept;

    MappedFile file_;
    FileHeader* header_;
    std::byte* data_start_;
    std::size_t capacity_;
};

} // namespace chronicle