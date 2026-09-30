#ifndef METRICS_COUNTER_HPP
#define METRICS_COUNTER_HPP

#include "metric.hpp"

namespace metrics {

// Monotonically increasing count. Never resets.
class Counter : public Metric {
public:
    explicit Counter(std::string name) : Metric(std::move(name)) {}

    void increment(long n = 1) noexcept { count_ += n; }

    [[nodiscard]] std::string_view typeName() const noexcept override { return "counter"; }
    [[nodiscard]] long value() const noexcept override { return count_; }
    [[nodiscard]] long sample(std::uint64_t) override { return count_; }

private:
    long count_{0};
};

} // namespace metrics

#endif // METRICS_COUNTER_HPP
