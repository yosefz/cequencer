#include "holocron/mappedFile.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace chronicle {

namespace {

[[noreturn]] void fatal(const char* msg, const char* path = nullptr) noexcept {
    std::fprintf(stderr, "chronicle: %s", msg);
    if (path) {
        std::fprintf(stderr, ": %s", path);
    }
#ifdef _WIN32
    std::fprintf(stderr, " (error=%lu)\n", GetLastError());
#else
    std::fprintf(stderr, " (errno=%d: %s)\n", errno, std::strerror(errno));
#endif
    std::abort();
}

} // anonymous namespace

#ifdef _WIN32

// Windows implementation
MappedFile MappedFile::open(const std::filesystem::path& path,
                            std::size_t size,
                            bool writable) {
    DWORD access = writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD creation = writable ? OPEN_ALWAYS : OPEN_EXISTING;

    HANDLE file = CreateFileW(
        path.c_str(),
        access,
        share,
        nullptr,
        creation,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        fatal("failed to open file", path.string().c_str());
    }

    // Get current file size
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size)) {
        CloseHandle(file);
        fatal("failed to get file size", path.string().c_str());
    }

    std::size_t current_size = static_cast<std::size_t>(file_size.QuadPart);

    // Extend file if needed
    if (writable && current_size < size) {
        LARGE_INTEGER new_size;
        new_size.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(file, new_size, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(file)) {
            CloseHandle(file);
            fatal("failed to extend file", path.string().c_str());
        }
        current_size = size;
    }

    if (current_size == 0) {
        current_size = size;
    }

    // Create file mapping
    DWORD protect = writable ? PAGE_READWRITE : PAGE_READONLY;
    HANDLE mapping = CreateFileMappingW(
        file,
        nullptr,
        protect,
        static_cast<DWORD>(current_size >> 32),
        static_cast<DWORD>(current_size & 0xFFFFFFFF),
        nullptr
    );

    if (mapping == nullptr) {
        CloseHandle(file);
        fatal("failed to create file mapping", path.string().c_str());
    }

    // Map view
    DWORD map_access = writable ? FILE_MAP_WRITE : FILE_MAP_READ;
    void* data = MapViewOfFile(mapping, map_access, 0, 0, current_size);

    if (data == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        fatal("failed to map file", path.string().c_str());
    }

    return MappedFile{data, current_size, file, mapping, path, writable};
}

MappedFile MappedFile::open_readonly(const std::filesystem::path& path) {
    return open(path, 0, false);
}

MappedFile::MappedFile(void* data, std::size_t size, void* fd, void* mapping,
                       std::filesystem::path path, bool writable) noexcept
    : data_{data}
    , size_{size}
    , fd_{fd}
    , mapping_{mapping}
    , path_{std::move(path)}
    , writable_{writable}
{}

MappedFile::~MappedFile() {
    if (data_) {
        UnmapViewOfFile(data_);
    }
    if (mapping_) {
        CloseHandle(static_cast<HANDLE>(mapping_));
    }
    if (fd_ && fd_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(fd_));
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_{std::exchange(other.data_, nullptr)}
    , size_{std::exchange(other.size_, 0)}
    , fd_{std::exchange(other.fd_, nullptr)}
    , mapping_{std::exchange(other.mapping_, nullptr)}
    , path_{std::move(other.path_)}
    , writable_{other.writable_}
{}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (data_) {
            UnmapViewOfFile(data_);
        }
        if (mapping_) {
            CloseHandle(static_cast<HANDLE>(mapping_));
        }
        if (fd_ && fd_ != INVALID_HANDLE_VALUE) {
            CloseHandle(static_cast<HANDLE>(fd_));
        }

        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        fd_ = std::exchange(other.fd_, nullptr);
        mapping_ = std::exchange(other.mapping_, nullptr);
        path_ = std::move(other.path_);
        writable_ = other.writable_;
    }
    return *this;
}

void MappedFile::sync() noexcept {
    if (data_) {
        FlushViewOfFile(data_, size_);
    }
}

#else

// Unix/Linux/macOS implementation
MappedFile MappedFile::open(const std::filesystem::path& path,
                            std::size_t size,
                            bool writable) {
    const char* cpath = path.c_str();

    int flags = writable ? (O_RDWR | O_CREAT) : O_RDONLY;
    int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;  // 0644

    int fd = ::open(cpath, flags, mode);
    if (fd < 0) {
        fatal("failed to open file", cpath);
    }

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        fatal("failed to stat file", cpath);
    }

    std::size_t file_size = static_cast<std::size_t>(st.st_size);

    // Only size a file we are creating. Growing one that already exists would
    // push writes past the range existing tailers mapped at their open time,
    // which reads as a SIGBUS on their side.
    if (writable && file_size == 0) {
        // Allocate the blocks up front so the first store to each page does not
        // have to do filesystem allocation on the hot path.
#if defined(__APPLE__)
        fstore_t store{F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, static_cast<off_t>(size), 0};
        if (::fcntl(fd, F_PREALLOCATE, &store) < 0) {
            store.fst_flags = F_ALLOCATEALL;  // retry without requiring contiguity
            (void)::fcntl(fd, F_PREALLOCATE, &store);
        }
#elif defined(__linux__)
        (void)::posix_fallocate(fd, 0, static_cast<off_t>(size));
#endif
        if (::ftruncate(fd, static_cast<off_t>(size)) < 0) {
            ::close(fd);
            fatal("failed to extend file", cpath);
        }
        file_size = size;
    }

    if (file_size == 0) {
        file_size = size;
    }

    int prot = PROT_READ | (writable ? PROT_WRITE : 0);
    int mflags = MAP_SHARED;

    void* data = ::mmap(nullptr, file_size, prot, mflags, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        fatal("failed to mmap file", cpath);
    }

    (void)::madvise(data, file_size, MADV_WILLNEED);
    (void)::madvise(data, file_size, MADV_SEQUENTIAL);

    if (writable) {
        // Pre-fault the mapping so publishing never stalls on a page fault.
        // One touch per page; the read-modify-write keeps existing bytes intact
        // when reopening a queue that already holds messages.
        auto* bytes = static_cast<volatile unsigned char*>(data);
        const std::size_t page = static_cast<std::size_t>(::getpagesize());
        for (std::size_t off = 0; off < file_size; off += page) {
            bytes[off] = bytes[off];
        }
    }

    return MappedFile{data, file_size, fd, path, writable};
}

MappedFile MappedFile::open_readonly(const std::filesystem::path& path) {
    return open(path, 0, false);
}

MappedFile::MappedFile(void* data, std::size_t size, int fd,
                       std::filesystem::path path, bool writable) noexcept
    : data_{data}
    , size_{size}
    , fd_{fd}
    , path_{std::move(path)}
    , writable_{writable}
{}

MappedFile::~MappedFile() {
    if (data_ && data_ != MAP_FAILED) {
        ::munmap(data_, size_);
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_{std::exchange(other.data_, nullptr)}
    , size_{std::exchange(other.size_, 0)}
    , fd_{std::exchange(other.fd_, -1)}
    , path_{std::move(other.path_)}
    , writable_{other.writable_}
{}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (data_ && data_ != MAP_FAILED) {
            ::munmap(data_, size_);
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }

        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        fd_ = std::exchange(other.fd_, -1);
        path_ = std::move(other.path_);
        writable_ = other.writable_;
    }
    return *this;
}

void MappedFile::sync() noexcept {
    if (data_ && data_ != MAP_FAILED) {
        ::msync(data_, size_, MS_SYNC);
    }
}

#endif

} // namespace chronicle