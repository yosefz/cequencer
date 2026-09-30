#include "apps/streamApp.hpp"

#include <iostream>

namespace apps {

    StreamApp::StreamApp(const std::string& name, const std::string& base_path,
                         config::ClockSource clock_source)
        : App(name, base_path)
        , clock_(std::shared_ptr<sequencer::Clock>(sequencer::createClock(clock_source).release())) {
    }

    void StreamApp::onTimeEvent(std::uint64_t timestamp) {
        current_time_ns_ = timestamp;
    }

    void StreamApp::onEndOfSession(const std::string& session_id, const std::string& reason) {
        (void)session_id;
        (void)reason;
        // Default: stop the app
        stop();
    }

    void StreamApp::markSessionStarted(const std::string& session_id) {
        session_started_ = true;
        session_id_ = session_id;
        std::cout << "Session started: " << session_id << "\n";
        onStartOfSession(session_id);
    }

    void StreamApp::markSessionEnded(const std::string& session_id, const std::string& reason) {
        session_ended_ = true;
        std::cout << "Session ended: " << session_id << " (reason: " << reason << ")\n";
        onEndOfSession(session_id, reason);
    }

    void StreamApp::markRecovered() {
        if (recovering_) {
            recovering_ = false;
            std::cout << "Recovery complete for " << name() << " - now live\n";
            onRecovered();
        }
    }

} // namespace apps