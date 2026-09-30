#include "zbuffer/zbuffer.hpp"

namespace zbuffer {

// make_unique would value-initialize, i.e. memset the whole buffer before a
// single byte is written into it.
ZBuffer::ZBuffer(std::size_t capacity)
        : storage_{std::make_unique_for_overwrite<std::uint8_t[]>(capacity)}
    , data_{storage_.get()}
    , capacity_{capacity}
    {}

} // namespace zbuffer