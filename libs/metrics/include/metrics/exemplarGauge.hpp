#ifndef METRICS_EXEMPLAR_GAUGE_HPP
#define METRICS_EXEMPLAR_GAUGE_HPP

#include "metric.hpp"

#include <utility>

namespace metrics {

// Highest value seen since the last dump, then back to zero. Use it for peaks that
// a plain gauge would miss between scrapes, like worst-case queue depth or latency.
//
// Deliberately not a Gauge: sample() resets, so it is not substitutable for one.
class ExemplarGauge : public Metric {
public:
    explicit ExemplarGauge(std::string name) : Metric(std::move(name)) {}

    void observe(long v) noexcept {
        if (v > max_) {
            max_ = v;
        }
    }

    [[nodiscard]] std::string_view typeName() const noexcept override { return "exemplar"; }
    [[nodiscard]] long value() const noexcept override { return max_; }
    [[nodiscard]] long sample(std::uint64_t) override { return std::exchange(max_, 0L); }

private:
    long max_{0};
};

} // namespace metrics

#endif // METRICS_EXEMPLAR_GAUGE_HPP
