#ifndef SEQUENCER_SEQUENCER_HPP
#define SEQUENCER_SEQUENCER_HPP

#include "clock.hpp"
#include "eventSystem.hpp"
#include "commandSystem.hpp"
#include <config/config.hpp>
#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sequencer {

struct SequencerStats {
    std::uint64_t commands_processed{0};
    std::uint64_t poll_cycles{0};
    std::uint64_t empty_polls{0};
    std::uint64_t errors{0};
};

// Manages command queues from connected apps and writes to the event stream.
class Sequencer {
public:
    explicit Sequencer(const config::SequencerConfig& config);
    Sequencer(const config::SequencerConfig& config, std::shared_ptr<Clock> clock);
    ~Sequencer();

    Sequencer(const Sequencer&) = delete;
    Sequencer& operator=(const Sequencer&) = delete;

    void initialize();
    std::size_t pollCommands();

    CommandSystem& commandSystem() { return *command_system_; }
    const CommandSystem& commandSystem() const { return *command_system_; }

    EventSystem& eventSystem() { return *event_system_; }
    const EventSystem& eventSystem() const { return *event_system_; }

    std::uint64_t now() const { return clock_->now(); }

    chronicle::Appender& eventStream() { return *event_stream_; }
    std::uint64_t currentSequence() const { return event_system_ ? event_system_->currentSequence() : 0; }

    // Derived on demand from the command system - see pollCommands().
    SequencerStats stats() const {
        if (!command_system_) {
            return {};
        }
        const auto& cs = command_system_->stats();
        return SequencerStats{
            .commands_processed = cs.commands_processed,
            .poll_cycles = cs.poll_cycles,
            .empty_polls = cs.empty_polls,
            .errors = cs.handler_errors + cs.verification_failures + cs.unknown_type_ids,
        };
    }

    const config::SequencerConfig& config() const { return config_; }
    bool isInitialized() const { return initialized_; }
    const std::string& sessionId() const { return session_id_; }

    Clock& clock() { return *clock_; }
    const Clock& clock() const { return *clock_; }

    void setClock(std::shared_ptr<Clock> clock) { clock_ = std::move(clock); }

private:
    void createDirectories();
    void openEventStream();
    void createEventSystem();
    void createCommandSystem();

    config::SequencerConfig config_;
    std::shared_ptr<Clock> clock_;

    std::unique_ptr<chronicle::Appender> event_stream_;
    std::unique_ptr<EventSystem> event_system_;
    std::unique_ptr<CommandSystem> command_system_;

    bool initialized_{false};
    std::string session_id_;
};

} // namespace sequencer

#endif // SEQUENCER_SEQUENCER_HPP