#ifndef APPS_SEQUENCER_APP_HPP
#define APPS_SEQUENCER_APP_HPP

#include "app.hpp"
#include "cpuRelax.hpp"
#include <sequencer/sequencer.hpp>
#include <sequencer/eventSystem.hpp>
#include <sequencer/commandSystem.hpp>
#include <config/config.hpp>

#include <cstdint>
#include <memory>

namespace apps {

using PublishStartOfSessionFn = void(*)(sequencer::EventSystem&, const std::string& sessionId);
using PublishTimeFn = void(*)(sequencer::EventSystem&);
using PublishEndOfSessionFn = void(*)(sequencer::EventSystem&, const std::string& sessionId, std::uint64_t eventCount, const std::string& reason);

class SequencerApp : public App {
public:
    explicit SequencerApp(const config::SequencerConfig& config);
    explicit SequencerApp(const std::filesystem::path& config_path);
    ~SequencerApp() override;

    sequencer::CommandSystem& commandSystem() { return sequencer_->commandSystem(); }
    const sequencer::CommandSystem& commandSystem() const { return sequencer_->commandSystem(); }

    sequencer::EventSystem& eventSystem() { return sequencer_->eventSystem(); }
    const sequencer::EventSystem& eventSystem() const { return sequencer_->eventSystem(); }

    void setPublishStartOfSession(PublishStartOfSessionFn fn) { publish_start_ = fn; }
    void setPublishTime(PublishTimeFn fn) { publish_time_ = fn; }
    void setPublishEndOfSession(PublishEndOfSessionFn fn) { publish_end_ = fn; }

    void setTimeEventInterval(std::uint64_t interval_ns);

    sequencer::Sequencer& sequencer() { return *sequencer_; }
    const sequencer::Sequencer& sequencer() const { return *sequencer_; }

    const config::SequencerConfig& config() const { return config_; }

    // Publishes EndOfSession before stopping.
    void shutdown(const std::string& reason = "Normal shutdown");

protected:
    void onInitialize() override;
    void onRun() override;
    void onShutdown() override;

private:
    config::SequencerConfig config_;
    std::unique_ptr<sequencer::Sequencer> sequencer_;

    PublishStartOfSessionFn publish_start_{nullptr};
    PublishTimeFn publish_time_{nullptr};
    PublishEndOfSessionFn publish_end_{nullptr};

    std::uint64_t time_event_interval_ns_{0};
    std::uint64_t last_time_event_ns_{0};
    std::string shutdown_reason_{"Normal shutdown"};
};

} // namespace apps

#endif // APPS_SEQUENCER_APP_HPP