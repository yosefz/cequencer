#ifndef APPS_EVENT_STREAM_APP_HPP
#define APPS_EVENT_STREAM_APP_HPP

#include "streamApp.hpp"
#include "pollable.hpp"
#include "commander.hpp"
#include <config/config.hpp>
#include <holocron/holocronQueue.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace apps {

struct EventStreamStats {
    std::uint64_t events_processed{0};
    std::uint64_t system_events{0};
    std::uint64_t poll_cycles{0};
    std::uint64_t empty_polls{0};
    std::uint64_t errors{0};
};

enum class SystemEventKind : std::uint8_t {
    NotHandled,    // pass through to onEvent()
    Handled,       // consumed, nothing further to do
    SessionStart,
    SessionEnd,
};

// Trivially copyable so it comes back in registers. The string_views point into
// the event buffer and are only valid for the duration of the handler call -
// markSessionStarted/markSessionEnded copy what they keep.
struct SystemEventResult {
    SystemEventKind kind{SystemEventKind::NotHandled};
    std::uint64_t timestamp{0};
    std::string_view session_id{};
    std::string_view end_reason{};
};

// A plain function pointer, like SequencerApp's PublishXFn: this is called for
// every event on the stream, so it is not somewhere to pay for std::function.
using SystemEventHandlerFn = SystemEventResult (*)(std::uint32_t type_id, const std::byte* data, std::size_t length);

// Returns string_view into event buffer memory (valid for duration of event processing).
using MsgSenderExtractorFn = std::function<std::string_view(std::uint32_t type_id, const std::byte* data, std::size_t length)>;

// Tails the event stream, dispatches events, and enforces one-in-flight command semantics.
// Commands are acked when an event arrives with msgSender matching the app name.
class EventStreamApp : public StreamApp {
public:
    explicit EventStreamApp(const config::StreamAppConfig& config);
    explicit EventStreamApp(const std::filesystem::path& config_path);
    ~EventStreamApp() override;

    void setSystemEventHandler(SystemEventHandlerFn handler);
    void setMsgSenderExtractor(MsgSenderExtractorFn extractor);
    void setCommandTimeoutHandler(CommandTimeoutFn handler);

    void addPollable(Pollable* pollable);
    void removePollable(Pollable* pollable);

    // Returns false if a command is already in-flight or app is recovering.
    template<typename CommandT>
    bool sendCommand(const CommandT& cmd, std::uint64_t nanos) {
        if (!commander_ || isRecovering()) return false;
        return commander_->send(cmd, nanos);
    }

    bool sendCommandRaw(const zbuffer::ZBuffer& buf, std::uint32_t type_id, std::uint64_t nanos) {
        if (!commander_ || isRecovering()) return false;
        return commander_->sendRaw(buf, type_id, nanos);
    }

    bool hasCommandInFlight() const { return commander_ && commander_->hasCommandInFlight(); }
    bool hasCommander() const { return commander_ != nullptr; }
    Commander* commander() { return commander_.get(); }

    const EventStreamStats& stats() const { return stats_; }
    const config::StreamAppConfig& streamConfig() const { return config_; }

    virtual std::size_t pollEvents();
    std::size_t pollPollables(std::uint64_t nanos);

protected:
    void onInitialize() override;
    void onRun() override;
    void onShutdown() override;

    /**
     * Called on each iteration with current time.
     * Polls event stream and pollables.
     */
    void onRunOne(std::uint64_t nanos) override;

    virtual void onEvent(std::uint64_t nanos, std::uint32_t type_id,
                         const std::byte* data, std::size_t length) = 0;

private:
    void dispatchEvent(std::uint32_t type_id, const std::byte* data, std::size_t length);
    void initializeCommander();
    void checkCommandAck(std::uint32_t type_id, const std::byte* data, std::size_t length);

    config::StreamAppConfig config_;
    std::unique_ptr<chronicle::Tailer> tailer_;
    std::unique_ptr<Commander> commander_;
    SystemEventHandlerFn system_event_handler_{nullptr};
    MsgSenderExtractorFn msg_sender_extractor_;
    CommandTimeoutFn command_timeout_handler_;
    std::vector<Pollable*> pollables_;
    EventStreamStats stats_;
};

} // namespace apps

#endif // APPS_EVENT_STREAM_APP_HPP