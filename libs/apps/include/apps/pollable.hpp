#ifndef SEQUENCER_POLLABLE_HPP
#define SEQUENCER_POLLABLE_HPP

#include <cstddef>
#include <cstdint>

namespace apps {

    class Pollable {
    public:
        virtual ~Pollable() = default;
        virtual std::size_t pollOne(std::uint64_t nanos) = 0;
    };

} // namespace sequencer

#endif // SEQUENCER_POLLABLE_HPP