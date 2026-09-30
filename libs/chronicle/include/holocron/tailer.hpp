#pragma once

#include "holocron/types.hpp"
#include "holocron/mappedFile.hpp"
#include "zbuffer/zbuffer.hpp"

#include <atomic>
#include <filesystem>
#include <vector>

namespace chronicle {

// Read-only tail of a memory-mapped queue file. Multiple tailers can read the same file independently.
// Not thread-safe; one thread per tailer.
class Tailer {
public:
    // from_end: start from current write position (new messages only)
    static Tailer open(const std::filesystem::path& path,
                       bool from_end = false);

    Tailer(Tailer&&) noexcept = default;
    Tailer& operator=(Tailer&&) noexcept = default;

    [[nodiscard]] MessageView poll() noexcept {
        // write_pos is the contended line: the appender stores to it on every
        // publish and every tailer reads it. Snapshot it once per drain rather
        // than once per message; the earlier acquire still covers everything
        // below the snapshot.
        if (read_pos_ >= write_pos_cache_) {
            write_pos_cache_ = header_->write_pos.load(std::memory_order_acquire);

            if (read_pos_ >= write_pos_cache_) {
                return {};
            }
        }

        const auto* msg_hdr = reinterpret_cast<const MessageHeader*>(data_start_ + read_pos_);

        // Unreachable in a well-formed stream: write_pos only advances after
        // length is set. Kept as a cheap guard; it costs one predicted branch
        // and the load is reused below.
        if (msg_hdr->length == 0) {  // write in progress
            return {};
        }

        // No acquire fence needed here: the acquire load of write_pos above is
        // the synchronizing edge, so the appender's header and payload writes
        // are already visible.
        const std::size_t len = msg_hdr->length;
        const std::uint32_t type_id = msg_hdr->type_id;
        const std::byte* payload = data_start_ + read_pos_ + sizeof(MessageHeader);

        read_pos_ += message_slot_size(len);

        return MessageView{payload, len, type_id};
    }

    [[nodiscard]] bool poll_into(zbuffer::ZBuffer& buf) noexcept {
        auto msg = poll();
        if (!msg) {
            return false;
        }
        buf.reset();
        (void)buf.write(msg.data(), msg.length());
        return true;
    }

    std::size_t poll_all(std::vector<zbuffer::ZBuffer*>& buffers,
                         std::size_t max_count = 0) noexcept {
        std::size_t count = 0;
        const std::size_t limit = (max_count > 0)
            ? std::min(max_count, buffers.size())
            : buffers.size();

        while (count < limit) {
            auto msg = poll();
            if (!msg) {
                break;
            }

            zbuffer::ZBuffer* buf = buffers[count];
            if (msg.length() > buf->capacity()) [[unlikely]] {
                read_pos_ -= message_slot_size(msg.length());
                break;
            }

            buf->reset();
            (void)buf->write(msg.data(), msg.length());
            ++count;
        }

        return count;
    }

    std::size_t poll_all(std::vector<MessageView>& views,
                         std::size_t max_count = 0) noexcept {
        views.clear();
        if (max_count > 0) {
            views.reserve(max_count);  // don't reallocate mid-drain
        }
        std::size_t count = 0;

        while (max_count == 0 || count < max_count) {
            auto msg = poll();
            if (!msg) {
                break;
            }
            views.push_back(msg);
            ++count;
        }

        return count;
    }

    [[nodiscard]] bool has_next() const noexcept {
        return read_pos_ < header_->write_pos.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t lag() const noexcept {
        const std::size_t write_pos = header_->write_pos.load(std::memory_order_acquire);
        return (write_pos > read_pos_) ? (write_pos - read_pos_) : 0;
    }

    [[nodiscard]] std::size_t position() const noexcept { return read_pos_; }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return file_.path();
    }

private:
    Tailer(MappedFile file, std::size_t start_pos) noexcept;

    MappedFile file_;
    const FileHeader* header_;
    const std::byte* data_start_;
    std::size_t read_pos_;
    std::size_t write_pos_cache_;  // last observed write_pos; refreshed when drained
};

} // namespace chronicle