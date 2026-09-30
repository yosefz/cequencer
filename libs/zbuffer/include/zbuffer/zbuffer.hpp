#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace zbuffer {

// Read-only, trivially-copyable view over bytes owned by someone else.
//
// Exists so readers can decode straight out of a memory-mapped queue, which is
// mapped PROT_READ: there is deliberately no way to write through this type, so
// a handler that tries to mutate the mapping fails to compile rather than
// taking a SIGBUS. Copying one costs three words and runs no destructor.
class ZBufferView {
public:
    constexpr ZBufferView() noexcept = default;

    constexpr ZBufferView(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    ZBufferView(const std::byte* data, std::size_t size) noexcept
        : data_(reinterpret_cast<const std::uint8_t*>(data)), size_(size) {}

    [[nodiscard]] ZBufferView slice(std::size_t offset, std::size_t len) const noexcept {
        return ZBufferView(data_ + offset, len);
    }

    [[nodiscard]] ZBufferView sliceReadable(std::size_t offset, std::size_t len) const noexcept {
        return ZBufferView(data_ + read_pos_ + offset, len);
    }

    [[nodiscard]] bool isValid() const noexcept { return data_ != nullptr; }

    [[nodiscard]] bool read(void* dest, std::size_t len) noexcept {
        if (len > readable()) [[unlikely]] {
            return false;
        }
        std::memcpy(dest, data_ + read_pos_, len);
        read_pos_ += len;
        return true;
    }

    template<typename T>
    [[nodiscard]] bool read_value(T& value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        return read(&value, sizeof(T));
    }

    [[nodiscard]] std::span<const std::byte> peek() const noexcept {
        return {reinterpret_cast<const std::byte*>(data_ + read_pos_), readable()};
    }

    [[nodiscard]] std::string_view peek_string() const noexcept {
        return {reinterpret_cast<const char*>(data_ + read_pos_), readable()};
    }

    // Trims at first null byte. Useful for fixed-length string fields.
    [[nodiscard]] std::string_view asString() const noexcept {
        const char* p = reinterpret_cast<const char*>(data_);
        return std::string_view(p, strnlen(p, size_));
    }

    bool skip(std::size_t n) noexcept {
        if (n > readable()) [[unlikely]] {
            return false;
        }
        read_pos_ += n;
        return true;
    }

    [[nodiscard]] std::int8_t getInt8() noexcept {
        std::int8_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::int16_t getInt16() noexcept {
        std::int16_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::int32_t getInt32() noexcept {
        std::int32_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::int64_t getInt64() noexcept {
        std::int64_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] long getLong() noexcept {
        long v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint8_t getUInt8() noexcept {
        std::uint8_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint16_t getUInt16() noexcept {
        std::uint16_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint32_t getUInt32() noexcept {
        std::uint32_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint64_t getUInt64() noexcept {
        std::uint64_t v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] float getFloat() noexcept {
        float v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] double getDouble() noexcept {
        double v{}; (void)read_value(v); return v;
    }
    [[nodiscard]] bool getBool() noexcept {
        return getUInt8() != 0;
    }
    template<typename E>
    [[nodiscard]] E getEnum() noexcept {
        static_assert(std::is_enum_v<E>);
        return static_cast<E>(getInt32());
    }

    [[nodiscard]] std::int32_t peekInt32(std::size_t offset = 0) const noexcept {
        std::int32_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] std::int64_t peekInt64(std::size_t offset = 0) const noexcept {
        std::int64_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] std::uint32_t peekUInt32(std::size_t offset = 0) const noexcept {
        std::uint32_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] std::uint64_t peekUInt64(std::size_t offset = 0) const noexcept {
        std::uint64_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] double peekDouble(std::size_t offset = 0) const noexcept {
        double v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return size_; }
    [[nodiscard]] std::size_t read_pos() const noexcept { return read_pos_; }
    [[nodiscard]] std::size_t readable() const noexcept { return size_ - read_pos_; }
    [[nodiscard]] bool empty() const noexcept { return readable() == 0; }

    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] const std::uint8_t* read_ptr() const noexcept { return data_ + read_pos_; }

    void set_read_pos(std::size_t pos) noexcept { read_pos_ = pos; }
    void advance_read(std::size_t n) noexcept { read_pos_ += n; }
    void reset() noexcept { read_pos_ = 0; }

    [[nodiscard]] bool equals(const ZBufferView& other) const noexcept {
        if (readable() != other.readable()) return false;
        return std::memcmp(read_ptr(), other.read_ptr(), readable()) == 0;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t read_pos_ = 0;
};

static_assert(std::is_trivially_copyable_v<ZBufferView>);

// Byte buffer with independent read and write positions.
//
//   [0 ... read_pos ... write_pos ... capacity]
//
// Memory is allocated once at construction. Non-owning views are created via slice().
// Not thread-safe.
class ZBuffer {
public:
    ZBuffer() noexcept
        : storage_(nullptr), data_(nullptr), capacity_(0), read_pos_(0), write_pos_(0) {}

    explicit ZBuffer(std::size_t capacity);

    // Non-owning view into existing memory.
    ZBuffer(std::uint8_t* data, std::size_t size, std::size_t readable_len) noexcept
        : storage_(nullptr), data_(data), capacity_(size), read_pos_(0), write_pos_(readable_len) {}

    ~ZBuffer() = default;

    ZBuffer(const ZBuffer&) = delete;
    ZBuffer& operator=(const ZBuffer&) = delete;
    ZBuffer(ZBuffer&&) noexcept = default;
    ZBuffer& operator=(ZBuffer&&) noexcept = default;

    [[nodiscard]] ZBuffer slice(std::size_t offset, std::size_t len) const noexcept {
        return ZBuffer(data_ + offset, len, len);
    }

    [[nodiscard]] ZBuffer sliceReadable(std::size_t offset, std::size_t len) const noexcept {
        return ZBuffer(data_ + read_pos_ + offset, len, len);
    }

    [[nodiscard]] bool isView() const noexcept { return storage_ == nullptr && data_ != nullptr; }
    [[nodiscard]] bool isValid() const noexcept { return data_ != nullptr; }

    [[nodiscard]] bool write(const void* data, std::size_t len) noexcept {
        if (len > writable()) [[unlikely]] {
            return false;
        }
        std::memcpy(data_ + write_pos_, data, len);
        write_pos_ += len;
        return true;
    }

    [[nodiscard]] bool write(std::span<const std::byte> data) noexcept {
        return write(data.data(), data.size());
    }

    [[nodiscard]] bool write(std::string_view sv) noexcept {
        return write(sv.data(), sv.size());
    }

    template<typename T>
    [[nodiscard]] bool write_value(T value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        return write(&value, sizeof(T));
    }

    [[nodiscard]] bool read(void* dest, std::size_t len) noexcept {
        if (len > readable()) [[unlikely]] {
            return false;
        }
        std::memcpy(dest, data_ + read_pos_, len);
        read_pos_ += len;
        return true;
    }

    template<typename T>
    [[nodiscard]] bool read_value(T& value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        return read(&value, sizeof(T));
    }

    [[nodiscard]] std::span<const std::byte> peek() const noexcept {
        return {reinterpret_cast<const std::byte*>(data_ + read_pos_), readable()};
    }

    [[nodiscard]] std::string_view peek_string() const noexcept {
        return {reinterpret_cast<const char*>(data_ + read_pos_), readable()};
    }

    // Trims at first null byte. Useful for fixed-length string fields.
    [[nodiscard]] std::string_view asString() const noexcept {
        const char* p = reinterpret_cast<const char*>(data_);
        return std::string_view(p, strnlen(p, capacity_));
    }

    bool skip(std::size_t n) noexcept {
        if (n > readable()) [[unlikely]] {
            return false;
        }
        read_pos_ += n;
        return true;
    }

    [[nodiscard]] std::int8_t getInt8() noexcept {
        std::int8_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::int16_t getInt16() noexcept {
        std::int16_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::int32_t getInt32() noexcept {
        std::int32_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::int64_t getInt64() noexcept {
        std::int64_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] long getLong() noexcept {
        long v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint8_t getUInt8() noexcept {
        std::uint8_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint16_t getUInt16() noexcept {
        std::uint16_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint32_t getUInt32() noexcept {
        std::uint32_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] std::uint64_t getUInt64() noexcept {
        std::uint64_t v; (void)read_value(v); return v;
    }
    [[nodiscard]] float getFloat() noexcept {
        float v; (void)read_value(v); return v;
    }
    [[nodiscard]] double getDouble() noexcept {
        double v; (void)read_value(v); return v;
    }
    [[nodiscard]] bool getBool() noexcept {
        return getUInt8() != 0;
    }
    template<typename E>
    [[nodiscard]] E getEnum() noexcept {
        static_assert(std::is_enum_v<E>);
        return static_cast<E>(getInt32());
    }

    bool putInt8(std::int8_t v) noexcept { return write_value(v); }
    bool putInt16(std::int16_t v) noexcept { return write_value(v); }
    bool putInt32(std::int32_t v) noexcept { return write_value(v); }
    bool putInt64(std::int64_t v) noexcept { return write_value(v); }
    bool putLong(long v) noexcept { return write_value(v); }
    bool putUInt8(std::uint8_t v) noexcept { return write_value(v); }
    bool putUInt16(std::uint16_t v) noexcept { return write_value(v); }
    bool putUInt32(std::uint32_t v) noexcept { return write_value(v); }
    bool putUInt64(std::uint64_t v) noexcept { return write_value(v); }
    bool putFloat(float v) noexcept { return write_value(v); }
    bool putDouble(double v) noexcept { return write_value(v); }
    bool putBool(bool v) noexcept { return putUInt8(v ? 1 : 0); }
    template<typename E>
    bool putEnum(E v) noexcept {
        static_assert(std::is_enum_v<E>);
        return putInt32(static_cast<std::int32_t>(v));
    }

    [[nodiscard]] std::int32_t peekInt32(std::size_t offset = 0) const noexcept {
        std::int32_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] std::int64_t peekInt64(std::size_t offset = 0) const noexcept {
        std::int64_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] std::uint32_t peekUInt32(std::size_t offset = 0) const noexcept {
        std::uint32_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] std::uint64_t peekUInt64(std::size_t offset = 0) const noexcept {
        std::uint64_t v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }
    [[nodiscard]] double peekDouble(std::size_t offset = 0) const noexcept {
        double v;
        std::memcpy(&v, data_ + read_pos_ + offset, sizeof(v));
        return v;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t read_pos() const noexcept { return read_pos_; }
    [[nodiscard]] std::size_t write_pos() const noexcept { return write_pos_; }
    [[nodiscard]] std::size_t readable() const noexcept { return write_pos_ - read_pos_; }
    [[nodiscard]] std::size_t writable() const noexcept { return capacity_ - write_pos_; }
    [[nodiscard]] bool empty() const noexcept { return readable() == 0; }

    void reset() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
    }

    void compact() noexcept {
        if (read_pos_ == 0) return;
        const std::size_t remaining = readable();
        if (remaining > 0) {
            std::memmove(data_, data_ + read_pos_, remaining);
        }
        read_pos_ = 0;
        write_pos_ = remaining;
    }

    [[nodiscard]] std::uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::uint8_t* write_ptr() noexcept { return data_ + write_pos_; }
    [[nodiscard]] const std::uint8_t* read_ptr() const noexcept { return data_ + read_pos_; }

    void set_write_pos(std::size_t pos) noexcept { write_pos_ = pos; }
    void set_read_pos(std::size_t pos) noexcept { read_pos_ = pos; }

    void advance_write(std::size_t n) noexcept { write_pos_ += n; }
    void advance_read(std::size_t n) noexcept { read_pos_ += n; }

    [[nodiscard]] bool equals(const ZBuffer& other) const noexcept {
        if (readable() != other.readable()) return false;
        return std::memcmp(read_ptr(), other.read_ptr(), readable()) == 0;
    }

    [[nodiscard]] bool copyFrom(const ZBuffer& other) noexcept {
        return write(other.read_ptr(), other.readable());
    }

    [[nodiscard]] bool copyFrom(const ZBuffer& other, std::size_t offset, std::size_t len) noexcept {
        if (offset + len > other.readable()) return false;
        return write(other.read_ptr() + offset, len);
    }

private:
    std::unique_ptr<std::uint8_t[]> storage_;
    std::uint8_t* data_;
    std::size_t capacity_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};

class ZBufferPool {
public:
    ZBufferPool(std::size_t count, std::size_t buffer_capacity)
        : buffer_capacity_(buffer_capacity) {
        buffers_.reserve(count);
        free_list_.reserve(count);

        for (std::size_t i = 0; i < count; ++i) {
            buffers_.push_back(std::make_unique<ZBuffer>(buffer_capacity));
            free_list_.push_back(buffers_.back().get());
        }
    }

    ZBufferPool(const ZBufferPool&) = delete;
    ZBufferPool& operator=(const ZBufferPool&) = delete;
    ZBufferPool(ZBufferPool&&) = delete;
    ZBufferPool& operator=(ZBufferPool&&) = delete;

    [[nodiscard]] ZBuffer* acquire() noexcept {
        if (free_list_.empty()) [[unlikely]] {
            return nullptr;
        }
        ZBuffer* buf = free_list_.back();
        free_list_.pop_back();
        buf->reset();
        return buf;
    }

    void release(ZBuffer* buf) noexcept {
        if (buf) {
            free_list_.push_back(buf);
        }
    }

    class ScopedBuffer {
    public:
        ScopedBuffer(ZBufferPool* pool, ZBuffer* buf) : pool_(pool), buf_(buf) {}
        ~ScopedBuffer() { if (pool_ && buf_) pool_->release(buf_); }

        ScopedBuffer(const ScopedBuffer&) = delete;
        ScopedBuffer& operator=(const ScopedBuffer&) = delete;
        ScopedBuffer(ScopedBuffer&& other) noexcept
            : pool_(other.pool_), buf_(other.buf_) {
            other.pool_ = nullptr;
            other.buf_ = nullptr;
        }
        ScopedBuffer& operator=(ScopedBuffer&& other) noexcept {
            if (this != &other) {
                if (pool_ && buf_) pool_->release(buf_);
                pool_ = other.pool_;
                buf_ = other.buf_;
                other.pool_ = nullptr;
                other.buf_ = nullptr;
            }
            return *this;
        }

        ZBuffer* get() noexcept { return buf_; }
        ZBuffer* operator->() noexcept { return buf_; }
        ZBuffer& operator*() noexcept { return *buf_; }
        explicit operator bool() const noexcept { return buf_ != nullptr; }

    private:
        ZBufferPool* pool_;
        ZBuffer* buf_;
    };

    [[nodiscard]] ScopedBuffer acquireScoped() noexcept {
        return ScopedBuffer(this, acquire());
    }

    [[nodiscard]] std::size_t size() const noexcept { return buffers_.size(); }
    [[nodiscard]] std::size_t available() const noexcept { return free_list_.size(); }
    [[nodiscard]] std::size_t inUse() const noexcept { return size() - available(); }
    [[nodiscard]] std::size_t bufferCapacity() const noexcept { return buffer_capacity_; }

private:
    std::size_t buffer_capacity_;
    std::vector<std::unique_ptr<ZBuffer>> buffers_;
    std::vector<ZBuffer*> free_list_;
};

} // namespace zbuffer