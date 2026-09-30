#include "holocron/appender.hpp"

#include <cstdlib>
#include <cstring>

namespace chronicle {

    Appender Appender::create(const std::filesystem::path& path, std::size_t size) {
        auto file = MappedFile::open(path, size, true);
        return Appender{std::move(file)};
    }

    Appender::Appender(MappedFile file) noexcept
        : file_{std::move(file)}
    , header_{static_cast<FileHeader*>(file_.data())}
    , data_start_{static_cast<std::byte*>(file_.data()) + FileHeader::data_offset()}
    , capacity_{file_.size() - FileHeader::data_offset()}
    {
        // Initialize header if new file
        if (header_->magic != queue_magic) {
            header_->magic = queue_magic;
            header_->version = queue_version;
            header_->capacity = file_.size();
            header_->write_pos.store(0, std::memory_order_release);
            std::memset(header_->reserved, 0, sizeof(header_->reserved));
        } else if (header_->version != queue_version) {
            std::abort();  // Version mismatch
        } else if (header_->capacity != file_.size()) {
            // The file was created at a different size than we have mapped.
            // Writing under that mismatch would put messages outside the range
            // existing tailers mapped, so refuse rather than corrupt them.
            std::abort();
        }
    }

} // namespace chronicle