#ifndef SEQUENCER_EVENT_SYSTEM_HPP
#define SEQUENCER_EVENT_SYSTEM_HPP

#include "clock.hpp"
#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <cstdint>
#include <cstring>

namespace sequencer {

// Pre-allocates a single buffer for event encoding. Automatically sets sequenceNumber and sendingTimestamp.
//
// The message type travels in the queue's MessageHeader, not in the payload, so
// the buffer holds the encoded event and nothing else.
//
// Base event layout:
//   [0..1]   messageVersion  (set by encoder.wrap())
//   [2..9]   sequenceNumber  (set by commitEvent())
//   [10..41] msgSender
//   [42..49] sendingTimestamp (set by commitEvent())
class EventSystem {
public:
    static constexpr std::size_t MESSAGE_VERSION_OFFSET = 0;
    static constexpr std::size_t SEQUENCE_NUMBER_OFFSET = 2;
    static constexpr std::size_t MSG_SENDER_OFFSET = 10;
    static constexpr std::size_t SENDING_TIMESTAMP_OFFSET = 42;
    static constexpr std::size_t BASE_EVENT_SIZE = 50;

    // appender and clock must outlive EventSystem
    EventSystem(chronicle::Appender& appender, Clock& clock, std::size_t max_event_size)
        : appender_(appender)
        , clock_(clock)
        , buffer_(max_event_size) {
        prepareBuffer();
    }

    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;
    EventSystem(EventSystem&&) = delete;
    EventSystem& operator=(EventSystem&&) = delete;

    // Call encoder.wrap() at this position.
    zbuffer::ZBuffer& buffer() noexcept { return buffer_; }

    std::uint64_t currentSequence() const noexcept { return sequence_; }
    std::uint64_t now() const noexcept { return clock_.now(); }

    template<typename EncoderT>
    void commitEvent(EncoderT& encoder) {
        auto seq = ++sequence_;
        auto ts = clock_.now();
        auto* msg_data = encoder.data();
        std::memcpy(msg_data + SEQUENCE_NUMBER_OFFSET, &seq, sizeof(seq));
        std::memcpy(msg_data + SENDING_TIMESTAMP_OFFSET, &ts, sizeof(ts));

        encoder.commit();
        appender_.write(buffer_, EncoderT::TYPE_ID);
        prepareBuffer();
    }

    // Skips auto-setting sequence/timestamp (for replay).
    template<typename EncoderT>
    void commitEventRaw(EncoderT& encoder) {
        encoder.commit();
        appender_.write(buffer_, EncoderT::TYPE_ID);
        prepareBuffer();
    }

    std::uint64_t nextSequence() noexcept { return ++sequence_; }
    void setSequence(std::uint64_t seq) noexcept { sequence_ = seq; }

private:
    void prepareBuffer() noexcept {
        buffer_.reset();
    }

    chronicle::Appender& appender_;
    Clock& clock_;
    zbuffer::ZBuffer buffer_;
    std::uint64_t sequence_{0};
};

} // namespace sequencer

#endif // SEQUENCER_EVENT_SYSTEM_HPP