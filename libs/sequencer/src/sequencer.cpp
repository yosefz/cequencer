#include "sequencer/sequencer.hpp"

#include <filesystem>
#include <iostream>

namespace sequencer {

Sequencer::Sequencer(const config::SequencerConfig& config)
    : config_(config)
    , clock_(std::shared_ptr<Clock>(createClock(config.clock_source).release())) {
}

Sequencer::Sequencer(const config::SequencerConfig& config, std::shared_ptr<Clock> clock)
    : config_(config)
    , clock_(std::move(clock)) {
}

Sequencer::~Sequencer() = default;

void Sequencer::initialize() {
    if (initialized_) return;

    createDirectories();
    openEventStream();
    createEventSystem();
    createCommandSystem();

    // TODO register validators

    // Generate session ID (timestamp-based)
    session_id_ = std::to_string(clock_->now()); // TODO make this a date

    initialized_ = true;
}

std::size_t Sequencer::pollCommands() {
    if (!initialized_) {
        throw std::runtime_error("Sequencer not initialized");
    }

    // Stats are derived from the command system on demand (see stats()) rather
    // than mirrored here - this runs on every poll cycle and nothing reads them
    // until shutdown.
    return command_system_->poll();
}

void Sequencer::createDirectories() {
    std::filesystem::create_directories(config_.commands_path());
    std::filesystem::create_directories(config_.events_path());
}

void Sequencer::openEventStream() {
    auto path = config_.event_stream_file();
    event_stream_ = std::make_unique<chronicle::Appender>(
        chronicle::Appender::create(path, config_.event_stream_size));
    std::cout << "  Opened event stream: " << path << "\n";
}

void Sequencer::createEventSystem() {
    event_system_ = std::make_unique<EventSystem>(
        *event_stream_, *clock_, config_.max_event_size);
    std::cout << "  Created EventSystem (max event size: " << config_.max_event_size << ")\n";
}

void Sequencer::createCommandSystem() {
    command_system_ = std::make_unique<CommandSystem>(config_.max_command_size);

    for (const auto& app : config_.apps) {
        command_system_->addQueue(app, config_.command_queue_file(app), config_.command_queue_size);
    }

    std::cout << "  Created CommandSystem (max command size: " << config_.max_command_size << ")\n";
}

} // namespace sequencer