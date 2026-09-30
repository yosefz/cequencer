#ifndef SEQUENCER_CLOCK_HPP
#define SEQUENCER_CLOCK_HPP

#include <config/config.hpp>
#include <chrono>
#include <cstdint>
#include <memory>

namespace sequencer {

/**
 * Abstract clock interface for sequencer timestamps.
 */
class Clock {
public:
    virtual ~Clock() = default;

    /**
     * Get current time in nanoseconds since epoch.
     */
    [[nodiscard]] virtual long now() const noexcept = 0;
};

/**
 * System clock implementation (wall-clock time).
 */
class SystemClock : public Clock {
public:
    [[nodiscard]] long now() const noexcept override {
        auto tp = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }
};

/**
 * Steady clock implementation (monotonic, not affected by system time changes).
 */
class SteadyClock : public Clock {
public:
    [[nodiscard]] long now() const noexcept override {
        auto tp = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }
};

/**
 * High resolution clock implementation.
 */
class HighResClock : public Clock {
public:
    [[nodiscard]] long now() const noexcept override {
        auto tp = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tp.time_since_epoch()).count();
    }
};

/**
 * Mock clock for testing - manually controlled time.
 *
 * Usage:
 *   auto clock = std::make_shared<MockClock>(1'000'000'000);  // Start at 1 second
 *   clock->advance(1'000'000);  // Advance 1ms
 *   clock->advanceMillis(100);  // Advance 100ms
 *   clock->set(2'000'000'000);  // Set to 2 seconds
 */
class MockClock : public Clock {
public:
    explicit MockClock(long initial_nanos = 0)
        : nanos_(initial_nanos) {}

    [[nodiscard]] long now() const noexcept override {
        return nanos_;
    }

    /**
     * Set the current time.
     */
    void set(long nanos) {
        nanos_ = nanos;
    }

    /**
     * Advance time by the given number of nanoseconds.
     */
    void advance(long nanos) {
        nanos_ += nanos;
    }

    /**
     * Advance time by the given number of microseconds.
     */
    void advanceMicros(long micros) {
        nanos_ += micros * 1'000;
    }

    /**
     * Advance time by the given number of milliseconds.
     */
    void advanceMillis(long millis) {
        nanos_ += millis * 1'000'000;
    }

    /**
     * Advance time by the given number of seconds.
     */
    void advanceSeconds(long seconds) {
        nanos_ += seconds * 1'000'000'000;
    }

private:
    long nanos_;
};

/**
 * Factory function to create clock from config.
 */
inline std::unique_ptr<Clock> createClock(config::ClockSource source) {
    switch (source) {
        case config::ClockSource::SYSTEM:
            return std::make_unique<SystemClock>();
        case config::ClockSource::STEADY:
            return std::make_unique<SteadyClock>();
        case config::ClockSource::HIGH_RES:
            return std::make_unique<HighResClock>();
    }
    return std::make_unique<SystemClock>();
}

} // namespace sequencer

#endif // SEQUENCER_CLOCK_HPP