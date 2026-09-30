#ifndef METRICS_METRIC_HPP
#define METRICS_METRIC_HPP

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace metrics {

// Base for all metrics.
//
// Recording lives on the concrete types as non-virtual inline methods so the hot
// path stays a direct call. Only reading is virtual, and that runs once per dump
// interval.
class Metric {
public:
    explicit Metric(std::string name) : name_(std::move(name)) {}
    virtual ~Metric() = default;

    Metric(const Metric&) = delete;
    Metric& operator=(const Metric&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] virtual std::string_view typeName() const noexcept = 0;

    // Current value, no side effects.
    [[nodiscard]] virtual long value() const noexcept = 0;

    // Value to publish. Metrics with per-interval semantics reset themselves here,
    // so call this once per dump and not for inspection.
    [[nodiscard]] virtual long sample(std::uint64_t nanos) = 0;

private:
    const std::string name_;
};

inline std::ostream& operator<<(std::ostream& os, const Metric& metric) {
    return os << "{name=" << metric.name()
              << ", type=" << metric.typeName()
              << ", value=" << metric.value() << "}";
}

} // namespace metrics

#endif // METRICS_METRIC_HPP
