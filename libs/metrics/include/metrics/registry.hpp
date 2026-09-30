#ifndef METRICS_REGISTRY_HPP
#define METRICS_REGISTRY_HPP

#include "metric.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace metrics {

// Owns an app's metrics and dumps them to a text file.
//
// The dump is written to a temporary file and renamed over the previous one, so a
// scraper reading concurrently never sees a partial file. Output is one line per
// metric:
//
//   app=sequencer metric=commands.processed type=counter value=1024 ts=1740000000000000000
//
// Not thread-safe. Record and dump from the same thread; hooking writeIfDue() into
// the app's poll loop keeps it that way and costs nothing.
class Registry {
public:
    static constexpr std::uint64_t DEFAULT_WRITE_INTERVAL_NS = 1'000'000'000ULL;

    Registry(std::string appName,
             const std::filesystem::path& directory,
             std::uint64_t writeIntervalNanos = DEFAULT_WRITE_INTERVAL_NS);

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // Constructs a metric in place and returns it by reference. The registry owns it;
    // the reference stays valid for the registry's lifetime.
    template<typename MetricT, typename... Args>
    MetricT& add(Args&&... args) {
        auto metric = std::make_unique<MetricT>(std::forward<Args>(args)...);
        MetricT& ref = *metric;
        metrics_.push_back(std::move(metric));
        return ref;
    }

    // Dumps if the write interval has elapsed. Returns true if it wrote.
    bool writeIfDue(std::uint64_t nanos);

    // Dumps unconditionally. Samples every metric, so per-interval metrics reset.
    void write(std::uint64_t nanos);

    [[nodiscard]] const std::string& appName() const noexcept { return app_name_; }
    [[nodiscard]] const std::filesystem::path& outputPath() const noexcept { return output_path_; }
    [[nodiscard]] std::size_t size() const noexcept { return metrics_.size(); }

private:
    std::vector<std::unique_ptr<Metric>> metrics_;
    std::string app_name_;
    std::filesystem::path output_path_;
    std::filesystem::path tmp_path_;
    std::uint64_t write_interval_ns_;
    std::uint64_t last_write_ns_{0};
    bool written_{false};
};

} // namespace metrics

#endif // METRICS_REGISTRY_HPP
