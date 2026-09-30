#include "apps/eventStreamApp.hpp"
#include <config/configLoader.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>

namespace apps {

EventStreamApp::EventStreamApp(const config::StreamAppConfig& config)
    : StreamApp(config.name, config.base_path, config.clock_source)
    , config_(config) {
}

EventStreamApp::EventStreamApp(const std::filesystem::path& config_path)
    : EventStreamApp(config::ConfigLoader::loadStreamAppConfig(config_path)) {
}

EventStreamApp::~EventStreamApp() {
    stop();
}

void EventStreamApp::setSystemEventHandler(SystemEventHandlerFn handler) {
    system_event_handler_ = handler;
}

void EventStreamApp::setMsgSenderExtractor(MsgSenderExtractorFn extractor) {
    msg_sender_extractor_ = std::move(extractor);
}

void EventStreamApp::setCommandTimeoutHandler(CommandTimeoutFn handler) {
    command_timeout_handler_ = std::move(handler);
}

void EventStreamApp::addPollable(Pollable* pollable) {
    pollables_.push_back(pollable);
}

void EventStreamApp::removePollable(Pollable* pollable) {
    pollables_.erase(
        std::remove(pollables_.begin(), pollables_.end(), pollable),
        pollables_.end());
}

void EventStreamApp::initializeCommander() {
    if (config_.command_queue_path.empty()) {
        std::cout << "  Commander: disabled (no command_queue_path configured)\n";
        return;
    }

    try {
        // Commander will retry for up to 1 minute (6 retries * 10 seconds)
        commander_ = std::make_unique<Commander>(
            config_.command_queue_path,
            config_.command_timeout_ns,
            10,  // retry_interval_sec
            6    // max_retries
        );

        // Set timeout handler if provided
        if (command_timeout_handler_) {
            commander_->setTimeoutHandler(command_timeout_handler_);
        }
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Failed to connect to command queue: " << e.what() << "\n";
        throw;
    }
}

void EventStreamApp::onInitialize() {
    // Handle recovery mode
    if (recoveryModeRequested()) {
        startRecovery();
        std::cout << "*** RECOVERY MODE ENABLED ***\n";
        std::cout << "  Commands will be blocked until stream is fully replayed\n";
        // Force start from beginning in recovery mode
        config_.start_from_beginning = true;
    }

    // Open event stream tailer
    tailer_ = std::make_unique<chronicle::Tailer>(
        chronicle::Tailer::open(config_.event_stream_path));

    if (!config_.start_from_beginning) {
        // Skip to end - consume all existing messages
        while (tailer_->poll()) {
            // Discard
        }
    }

    std::cout << "  Event stream: " << config_.event_stream_path << "\n";
    std::cout << "  Start from: " << (config_.start_from_beginning ? "beginning" : "end") << "\n";
    std::cout << "  Command timeout: " << config_.command_timeout_ns / 1'000'000 << " ms\n";
    std::cout << "  Pollables: " << pollables_.size() << "\n";

    // Initialize commander (connect to command queue)
    initializeCommander();
}

void EventStreamApp::onRun() {
    while (isRunning()) {
        // Get current time - either from last Time event or system clock
        std::uint64_t nanos = currentTime();
        if (nanos == 0) {
            nanos = clock().now();
        }

        // Call the main runOne logic
        onRunOne(nanos);

        // Sleep if configured and no work done
        if (config_.poll_sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(config_.poll_sleep_us));
        }
    }
}

void EventStreamApp::onRunOne(std::uint64_t nanos) {
    stats_.poll_cycles++;

    // Poll event stream
    std::size_t events = pollEvents();

    // Check for recovery completion (stream drained)
    if (isRecovering() && events == 0) {
        markRecovered();
    }

    // Poll all pollables with current time
    std::size_t polled = pollPollables(nanos);

    // Check for command timeout (delegated to Commander)
    if (commander_) {
        commander_->checkTimeout(nanos);
    }

    if (events == 0 && polled == 0) {
        stats_.empty_polls++;
    }
}

std::size_t EventStreamApp::pollEvents() {
    std::size_t processed = 0;

    while (auto view = tailer_->poll()) {
        // TYPE_ID comes from the message header, so the view is the event itself
        dispatchEvent(view.type_id(), view.data(), view.length());

        stats_.events_processed++;
        processed++;
    }

    return processed;
}

void EventStreamApp::dispatchEvent(std::uint32_t type_id, const std::byte* data, std::size_t length) {
    // Try system event handler first
    if (system_event_handler_) {
        const auto result = system_event_handler_(type_id, data, length);

        if (result.kind != SystemEventKind::NotHandled) {
            stats_.system_events++;

            // Update time if this was a Time event
            if (result.timestamp > 0) {
                setCurrentTime(result.timestamp);
                onTimeEvent(result.timestamp);
            }

            if (result.kind == SystemEventKind::SessionStart) {
                markSessionStarted(std::string(result.session_id));
            } else if (result.kind == SystemEventKind::SessionEnd) {
                markSessionEnded(std::string(result.session_id),
                                 std::string(result.end_reason));
            }

            return;  // System event handled, don't pass to onEvent
        }
    }

    // Check if this event acknowledges our pending command
    checkCommandAck(type_id, data, length);

    // Call user's onEvent handler with current time
    onEvent(currentTime(), type_id, data, length);
}

void EventStreamApp::checkCommandAck(std::uint32_t type_id, const std::byte* data, std::size_t length) {
    if (!commander_ || !commander_->hasCommandInFlight() || !msg_sender_extractor_) {
        return;
    }

    std::string_view sender = msg_sender_extractor_(type_id, data, length);

    // Check if the sender matches our app name
    if (sender == name()) {
        commander_->onAck();
    }
}

std::size_t EventStreamApp::pollPollables(std::uint64_t nanos) {
    std::size_t total = 0;
    for (auto* pollable : pollables_) {
        total += pollable->pollOne(nanos);
    }
    return total;
}

void EventStreamApp::onShutdown() {
    std::cout << "\nStatistics:\n";
    std::cout << "  Events processed: " << stats_.events_processed << "\n";
    std::cout << "  System events: " << stats_.system_events << "\n";
    if (commander_) {
        std::cout << "  Commands sent: " << commander_->commandsSent() << "\n";
        std::cout << "  Commands acked: " << commander_->commandsAcked() << "\n";
        std::cout << "  Command timeouts: " << commander_->commandTimeouts() << "\n";
    }
    std::cout << "  Poll cycles: " << stats_.poll_cycles << "\n";
    std::cout << "  Empty polls: " << stats_.empty_polls << "\n";
    std::cout << "  Errors: " << stats_.errors << "\n";
}

} // namespace apps