#include "holocron/tailer.hpp"

#include <cstdlib>

namespace chronicle {

    Tailer Tailer::open(const std::filesystem::path& path, bool from_end) {
        auto file = MappedFile::open_readonly(path);

        const auto* header = static_cast<const FileHeader*>(file.data());

        if (header->magic != queue_magic) {
            std::abort();  // Not a valid queue file
        }

        if (header->version != queue_version) {
            std::abort();  // Version mismatch
        }

        // The mapping is sized at open time. If the file has since been resized
        // out from under us, every offset we derive is wrong and reads past the
        // mapping would SIGBUS, so refuse to open rather than corrupt.
        if (header->capacity != file.size()) {
            std::abort();
        }

        const std::size_t start = from_end
            ? header->write_pos.load(std::memory_order_acquire)
            : 0;

        return Tailer{std::move(file), start};
    }

    Tailer::Tailer(MappedFile file, std::size_t start_pos) noexcept
        : file_{std::move(file)}
    , header_{static_cast<const FileHeader*>(file_.data())}
    , data_start_{static_cast<const std::byte*>(file_.data()) + FileHeader::data_offset()}
    , read_pos_{start_pos}
    , write_pos_cache_{start_pos}
{}

} // namespace chronicle
