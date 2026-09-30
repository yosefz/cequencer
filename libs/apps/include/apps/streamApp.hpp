#ifndef APPS_STREAM_APP_HPP
#define APPS_STREAM_APP_HPP

#include "app.hpp"
#include <sequencer/clock.hpp>
#include <config/config.hpp>

#include <cstdint>
#include <memory>

namespace apps {

// Base for apps that consume the event stream with time-based callbacks.
// In recovery mode, events are replayed from the start and commands are blocked until onRecovered().
class StreamApp : public App {
public:
    virtual ~StreamApp() override = default;

    // Time from the last Time event, or system clock if none received.
    std::uint64_t currentTime() const { return current_time_ns_; }
    bool isSessionStarted() const { return session_started_; }
    const std::string& sessionId() const { return session_id_; }

    // Must be called before initialize().
    void setClock(std::shared_ptr<sequencer::Clock> clock) { clock_ = std::move(clock); }
    void setRecoveryMode(bool recovery) { recovery_mode_ = recovery; }

    bool isRecovering() const { return recovering_; }

protected:
    StreamApp(const std::string& name, const std::string& base_path,
              config::ClockSource clock_source = config::ClockSource::SYSTEM);

    virtual void onRunOne(std::uint64_t nanos) = 0;

    virtual void onStartOfSession(const std::string& session_id) { (void)session_id; }

    // Default implementation stops the app.
    virtual void onEndOfSession(const std::string& session_id, const std::string& reason);

    // Called when stream is fully replayed. Commands are allowed after this returns.
    virtual void onRecovered() {}

    // Default: updates current_time_ns_.
    virtual void onTimeEvent(std::uint64_t timestamp);

    void setCurrentTime(std::uint64_t nanos) { current_time_ns_ = nanos; }
    void markSessionStarted(const std::string& session_id);
    void markSessionEnded(const std::string& session_id, const std::string& reason);
    bool isSessionEnded() const { return session_ended_; }
    void markRecovered();
    bool recoveryModeRequested() const { return recovery_mode_; }
    void startRecovery() { recovering_ = true; }
    sequencer::Clock& clock() { return *clock_; }

private:
    std::shared_ptr<sequencer::Clock> clock_;
    std::uint64_t current_time_ns_{0};
    bool session_started_{false};
    bool session_ended_{false};
    bool recovery_mode_{false};   // Set before initialize()
    bool recovering_{false};       // True while recovering, false after onRecovered()
    std::string session_id_;
};

} // namespace apps

#endif // APPS_STREAM_APP_HPP