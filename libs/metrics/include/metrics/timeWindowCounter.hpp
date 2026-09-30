#ifndef METRICS_TIME_WINDOW_COUNTER_HPP
#define METRICS_TIME_WINDOW_COUNTER_HPP

#include "metric.hpp"

#include <cstddef>
#include <vector>

namespace metrics {

// Count over a rolling window of the last N seconds.
//
// Backed by a ring of one-second buckets allocated up front, so recording is O(1)
// and never allocates. Time is passed in rather than read from a clock: callers on
// the event path already hold the current nanos (see apps::Pollable::pollOne).
class TimeWindowCounter : public Metric {
public:
    static constexpr std::uint64_t ONE_SECOND_NS = 1'000'000'000ULL;

    TimeWindowCounter(std::string name, int windowSeconds)
        : Metric(std::move(name))
        , buckets_(windowSeconds > 0 ? static_cast<std::size_t>(windowSeconds) : 1, 0) {}

    void increment(long n, std::uint64_t nanos) noexcept {
        roll(nanos);
        buckets_[head_] += n;
        total_ += n;
    }

    [[nodiscard]] std::string_view typeName() const noexcept override { return "window"; }

    // May be stale by up to a window if nothing has been recorded recently;
    // sample() is the accurate read.
    [[nodiscard]] long value() const noexcept override { return total_; }

    [[nodiscard]] long sample(std::uint64_t nanos) override {
        roll(nanos);
        return total_;
    }

    [[nodiscard]] std::size_t windowSeconds() const noexcept { return buckets_.size(); }

private:
    // Advances the ring to the current second, evicting whatever the reused buckets
    // were holding. A gap longer than the window clears the ring exactly once.
    void roll(std::uint64_t nanos) noexcept {
        const std::uint64_t sec = nanos / ONE_SECOND_NS;
        if (sec == head_sec_ && started_) [[likely]] {
            return;
        }
        if (!started_) {
            started_ = true;
            head_sec_ = sec;
            return;
        }

        const std::uint64_t elapsed = sec - head_sec_;
        const std::size_t steps = elapsed < buckets_.size()
            ? static_cast<std::size_t>(elapsed)
            : buckets_.size();

        for (std::size_t i = 0; i < steps; ++i) {
            head_ = (head_ + 1) % buckets_.size();
            total_ -= buckets_[head_];
            buckets_[head_] = 0;
        }
        head_sec_ = sec;
    }

    std::vector<long> buckets_;
    std::size_t head_{0};
    std::uint64_t head_sec_{0};
    long total_{0};
    bool started_{false};
};

} // namespace metrics

#endif // METRICS_TIME_WINDOW_COUNTER_HPP
