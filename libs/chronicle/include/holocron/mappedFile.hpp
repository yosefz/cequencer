#pragma once

#include "holocron/types.hpp"

#include <cstddef>
#include <filesystem>
#include <utility>

namespace chronicle {

class MappedFile {
public:
    // writable=true creates the file if needed. Crashes on failure.
    static MappedFile open(const std::filesystem::path& path,
                           std::size_t size,
                           bool writable);

    static MappedFile open_readonly(const std::filesystem::path& path);

    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    [[nodiscard]] void* data() noexcept { return data_; }
    [[nodiscard]] const void* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] bool writable() const noexcept { return writable_; }

    void sync() noexcept;

private:
#ifdef _WIN32
    MappedFile(void* data, std::size_t size, void* fd, void* mapping,
               std::filesystem::path path, bool writable) noexcept;

    void* data_ = nullptr;
    std::size_t size_ = 0;
    void* fd_ = nullptr;       // HANDLE
    void* mapping_ = nullptr;  // HANDLE
    std::filesystem::path path_;
    bool writable_ = false;
#else
    MappedFile(void* data, std::size_t size, int fd,
               std::filesystem::path path, bool writable) noexcept;

    void* data_ = nullptr;
    std::size_t size_ = 0;
    int fd_ = -1;
    std::filesystem::path path_;
    bool writable_ = false;
#endif
};

} // namespace chronicle