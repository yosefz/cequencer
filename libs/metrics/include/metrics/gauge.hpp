#ifndef METRICS_GAUGE_HPP
#define METRICS_GAUGE_HPP

#include "metric.hpp"

namespace metrics {

// Value that moves in both directions (queue depth, in-flight commands, ...).
class Gauge : public Metric {
public:
    explicit Gauge(std::string name) : Metric(std::move(name)) {}

    void set(long v) noexcept { value_ = v; }
    void add(long n) noexcept { value_ += n; }
    void subtract(long n) noexcept { value_ -= n; }

    [[nodiscard]] std::string_view typeName() const noexcept override { return "gauge"; }
    [[nodiscard]] long value() const noexcept override { return value_; }
    [[nodiscard]] long sample(std::uint64_t) override { return value_; }

private:
    long value_{0};
};

} // namespace metrics

#endif // METRICS_GAUGE_HPP
