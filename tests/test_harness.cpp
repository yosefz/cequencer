#ifndef APPS_TEST_HARNESS_HPP
#define APPS_TEST_HARNESS_HPP

#include <sequencer/clock.hpp>
#include <sequencer/sequencer.hpp>
#include <config/config.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace apps {

/**
 * Interface for apps that can be driven by the test harness.
 */
class TestableApp {
public:
    virtual ~TestableApp() = default;

    /**
     * Run one iteration of the app's main loop.
     * @param nanos Current time in nanoseconds
     * @return Number of items processed (events, commands, etc.)
     */
    virtual std::size_t runOne(std::uint64_t nanos) = 0;

    /**
     * Check if the app has pending work.
     */
    virtual bool hasPendingWork() const = 0;

    /**
     * Get the app's name.
     */
    virtual const std::string& name() const = 0;
};

/**
 * Test harness for integration testing with controlled time.
 *
 * Manages a sequencer and multiple apps with a shared mock clock.
 * Provides methods to step through time and process events deterministically.
 *
 * Usage:
 *   TestHarness harness;
 *   harness.createSequencer(config);
 *
 *   auto& app1 = harness.addApp<MyApp>(app_config);
 *   auto& app2 = harness.addApp<OtherApp>(other_config);
 *
 *   harness.initialize();
 *
 *   // Run a single cycle
 *   harness.stepOnce();
 *
 *   // Run until all events processed
 *   harness.stepUntilIdle();
 *
 *   // Run for simulated time
 *   harness.stepFor(std::chrono::milliseconds(100));
 */
class TestHarness {
public:
    /**
     * Create a test harness with a mock clock starting at the given time.
     */
    explicit TestHarness(std::uint64_t initial_time_ns = 1'000'000'000)  // Default: 1 second
        : clock_(std::make_shared<sequencer::MockClock>(initial_time_ns)) {}

    /**
     * Create a test harness with a temp directory.
     */
    TestHarness(const std::string& test_name, std::uint64_t initial_time_ns = 1'000'000'000)
        : clock_(std::make_shared<sequencer::MockClock>(initial_time_ns))
        , temp_dir_(std::filesystem::temp_directory_path() / ("test_harness_" + test_name + "_" + std::to_string(counter_++)))
        , owns_temp_dir_(true) {
        std::filesystem::create_directories(temp_dir_);
    }

    ~TestHarness() {
        if (owns_temp_dir_ && std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    }

    // Non-copyable, movable
    TestHarness(const TestHarness&) = delete;
    TestHarness& operator=(const TestHarness&) = delete;
    TestHarness(TestHarness&&) = default;
    TestHarness& operator=(TestHarness&&) = default;

    /**
     * Get the shared mock clock.
     */
    std::shared_ptr<sequencer::MockClock> clock() { return clock_; }

    /**
     * Get current time.
     */
    std::uint64_t now() const { return clock_->now(); }

    /**
     * Get the temp directory path.
     */
    const std::filesystem::path& tempDir() const { return temp_dir_; }

    /**
     * Create a sequencer config for this harness.
     */
    config::SequencerConfig makeSequencerConfig(const std::vector<std::string>& app_names) {
        config::SequencerConfig cfg;
        cfg.name = "test_sequencer";
        cfg.base_path = temp_dir_.string();
        cfg.apps = app_names;
        cfg.poll_sleep_us = 0;  // No sleeping in tests
        return cfg;
    }

    /**
     * Create a stream app config for this harness.
     */
    config::StreamAppConfig makeStreamAppConfig(const std::string& app_name) {
        config::StreamAppConfig cfg;
        cfg.name = app_name;
        cfg.base_path = temp_dir_.string();
        cfg.event_stream_path = temp_dir_.string() + "/events/test_sequencer.dat";
        cfg.command_queue_path = temp_dir_.string() + "/commands/" + app_name + ".dat";
        cfg.poll_sleep_us = 0;  // No sleeping in tests
        cfg.start_from_beginning = true;
        return cfg;
    }

    /**
     * Create and set the sequencer.
     */
    void createSequencer(const config::SequencerConfig& config) {
        sequencer_ = std::make_unique<sequencer::Sequencer>(config, clock_);
    }

    /**
     * Get the sequencer.
     */
    sequencer::Sequencer* sequencer() { return sequencer_.get(); }

    /**
     * Get the command system (for registering handlers).
     */
    sequencer::CommandSystem* commandSystem() {
        return sequencer_ ? &sequencer_->commandSystem() : nullptr;
    }

    /**
     * Add a testable app.
     */
    void addApp(TestableApp* app) {
        apps_.push_back(app);
    }

    /**
     * Initialize the sequencer.
     */
    void initialize() {
        if (sequencer_) {
            sequencer_->initialize();
        }
    }

    /**
     * Step once - process one cycle for sequencer and all apps.
     * @return Total items processed across all components
     */
    std::size_t stepOnce() {
        std::size_t total = 0;
        std::uint64_t nanos = clock_->now();

        // Process sequencer commands
        if (sequencer_) {
            total += sequencer_->pollCommands();
        }

        // Process all apps
        for (auto* app : apps_) {
            total += app->runOne(nanos);
        }

        return total;
    }

    /**
     * Step until idle - keep stepping until no work is done.
     * @param max_iterations Maximum iterations to prevent infinite loops
     * @return Total items processed
     */
    std::size_t stepUntilIdle(std::size_t max_iterations = 10000) {
        std::size_t total = 0;
        std::size_t iterations = 0;

        while (iterations < max_iterations) {
            std::size_t processed = stepOnce();
            total += processed;

            if (processed == 0) {
                break;
            }

            iterations++;
        }

        return total;
    }

    /**
     * Step for a given duration of simulated time.
     * Time advances in increments, with processing at each step.
     *
     * @param duration Duration to simulate
     * @param time_step_ns Time increment per step (default 1ms)
     * @return Total items processed
     */
    template<typename Duration>
    std::size_t stepFor(Duration duration, std::uint64_t time_step_ns = 1'000'000) {
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        return stepForNanos(static_cast<std::uint64_t>(nanos), time_step_ns);
    }

    /**
     * Step for a given number of nanoseconds.
     */
    std::size_t stepForNanos(std::uint64_t duration_ns, std::uint64_t time_step_ns = 1'000'000) {
        std::size_t total = 0;
        std::uint64_t end_time = clock_->now() + duration_ns;

        while (clock_->now() < end_time) {
            total += stepOnce();
            clock_->advance(time_step_ns);
        }

        return total;
    }

    /**
     * Step for milliseconds.
     */
    std::size_t stepForMillis(std::uint64_t millis) {
        return stepForNanos(millis * 1'000'000);
    }

    /**
     * Advance time without processing.
     */
    void advanceTime(std::uint64_t nanos) {
        clock_->advance(nanos);
    }

    /**
     * Advance time by milliseconds without processing.
     */
    void advanceTimeMillis(std::uint64_t millis) {
        clock_->advanceMillis(millis);
    }

    /**
     * Check if any app has pending work.
     */
    bool hasPendingWork() const {
        for (const auto* app : apps_) {
            if (app->hasPendingWork()) {
                return true;
            }
        }
        return false;
    }

    /**
     * Get stats summary.
     */
    struct Stats {
        std::uint64_t commands_processed{0};
        std::uint64_t events_written{0};
    };

    Stats stats() const {
        Stats s;
        if (sequencer_) {
            s.commands_processed = sequencer_->stats().commands_processed;
            s.events_written = sequencer_->stats().events_written;
        }
        return s;
    }

private:
    std::shared_ptr<sequencer::MockClock> clock_;
    std::filesystem::path temp_dir_;
    bool owns_temp_dir_{false};
    std::unique_ptr<sequencer::Sequencer> sequencer_;
    std::vector<TestableApp*> apps_;

    static inline int counter_{0};
};

} // namespace apps

#endif // APPS_TEST_HARNESS_HPP