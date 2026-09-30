#ifndef CONFIG_CONFIG_HPP
#define CONFIG_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace config {

enum class ClockSource {
    SYSTEM,      // std::chrono::system_clock
    STEADY,      // std::chrono::steady_clock
    HIGH_RES,    // std::chrono::high_resolution_clock
};

struct AppConfig {
    std::string name{"app"};
    std::string base_path{"./data"};

    virtual ~AppConfig() = default;
};

struct SequencerConfig : AppConfig {
    // Performance tuning
    std::size_t max_command_size{4096};
    std::size_t max_event_size{64 * 1024};             // encode buffer for one event
    std::size_t command_queue_size{64 * 1024 * 1024};  // 64MB per queue
    std::size_t event_stream_size{256 * 1024 * 1024};  // 256MB
    std::uint32_t poll_sleep_us{0};  // 0 = busy spin (pins core)

    // Clock
    ClockSource clock_source{ClockSource::SYSTEM};

    // Time event publishing interval (0 = disabled)
    std::uint64_t time_event_interval_ns{0};

    // Connected apps (each gets a command queue)
    std::vector<std::string> apps{};

    // Derived paths
    std::string commands_path() const { return base_path + "/commands"; }
    std::string events_path() const { return base_path + "/events"; }
    std::string event_stream_file() const { return events_path() + "/" + name + ".dat"; }
    std::string command_queue_file(const std::string& app) const {
        return commands_path() + "/" + app + ".dat";
    }
};

struct StreamAppConfig : AppConfig {
    std::string event_stream_path{};
    std::string command_queue_path{};  // Path to this app's command queue
    bool start_from_beginning{false};
    std::uint32_t poll_sleep_us{1000};  // Default 1ms
    ClockSource clock_source{ClockSource::SYSTEM};

    // Command flow control
    std::uint64_t command_timeout_ns{5'000'000'000};  // Default 5 seconds
};

using EventStreamAppConfig = StreamAppConfig;

} // namespace config

#endif // CONFIG_CONFIG_HPP