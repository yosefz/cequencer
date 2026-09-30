#include "apps/sequencerApp.hpp"
#include <config/configLoader.hpp>

#include <iostream>
#include <thread>

namespace apps {

SequencerApp::SequencerApp(const config::SequencerConfig& config)
    : App(config.name, config.base_path)
    , config_(config)
    , sequencer_(std::make_unique<sequencer::Sequencer>(config)) {
}

SequencerApp::SequencerApp(const std::filesystem::path& config_path)
    : SequencerApp(config::ConfigLoader::loadSequencerConfig(config_path)) {
}

SequencerApp::~SequencerApp() {
    stop();
}

void SequencerApp::setTimeEventInterval(std::uint64_t interval_ns) {
    time_event_interval_ns_ = interval_ns;
}

void SequencerApp::shutdown(const std::string& reason) {
    shutdown_reason_ = reason;
    stop();
}

void SequencerApp::onInitialize() {
    sequencer_->initialize();

    std::cout << "  Command queues: " << config_.apps.size() << "\n";
    std::cout << "  Event stream: " << config_.event_stream_file() << "\n";
}

void SequencerApp::onRun() {
    std::cout << "Listening for commands from: ";
    for (const auto& app : config_.apps) {
        std::cout << app << " ";
    }
    std::cout << "\n\n";

    // Publish StartOfSession
    if (publish_start_) {
        publish_start_(sequencer_->eventSystem(), sequencer_->sessionId());
        std::cout << "Published StartOfSession event\n";
    }

    last_time_event_ns_ = sequencer_->now();

    // Reading the clock is a virtual call plus a timebase read, and the loop
    // period is the sequencer's reaction latency, so only check it periodically.
    constexpr std::uint64_t clock_check_mask = 1023;
    std::uint64_t spins = 0;

    while (isRunning()) {
        auto processed = sequencer_->pollCommands();

        // Publish Time event if interval elapsed
        if (time_event_interval_ns_ > 0 && publish_time_ &&
            ((spins++ & clock_check_mask) == 0 || processed == 0)) {
            auto now = sequencer_->now();
            if (now - last_time_event_ns_ >= time_event_interval_ns_) {
                publish_time_(sequencer_->eventSystem());
                last_time_event_ns_ = now;
            }
        }

        // Sleep if configured and no work done
        if (processed == 0 && config_.poll_sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(config_.poll_sleep_us));
        } else if (processed == 0) {
            cpuRelax();  // busy-spin: back off so we stop stealing the queue's cache line
        }
    }
}

void SequencerApp::onShutdown() {
    // Publish EndOfSession before printing stats
    if (publish_end_) {
        publish_end_(sequencer_->eventSystem(),
                     sequencer_->sessionId(),
                     sequencer_->eventSystem().currentSequence(),
                     shutdown_reason_);
        std::cout << "Published EndOfSession event (reason: " << shutdown_reason_ << ")\n";
    }

    const auto& stats = sequencer_->stats();
    std::cout << "\nStatistics:\n";
    std::cout << "  Commands processed: " << stats.commands_processed << "\n";
    std::cout << "  Events written: " << sequencer_->eventSystem().currentSequence() << "\n";
    std::cout << "  Poll cycles: " << stats.poll_cycles << "\n";
    std::cout << "  Empty polls: " << stats.empty_polls << "\n";
    std::cout << "  Errors: " << stats.errors << "\n";
}

} // namespace apps