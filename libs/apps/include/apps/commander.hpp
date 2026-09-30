#ifndef APPS_COMMANDER_HPP
#define APPS_COMMANDER_HPP

#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace apps {

using CommandTimeoutFn = std::function<void(std::uint64_t nanos, std::uint64_t elapsed_ns)>;

inline void defaultCommandTimeoutHandler(std::uint64_t /*nanos*/, std::uint64_t elapsed_ns) {
    std::cerr << "FATAL: Command timeout after " << elapsed_ns / 1'000'000 << " ms\n";
    std::abort();
}

// Sends commands to the sequencer with one-in-flight enforcement.
// Waits for the command queue to be created (retries every retry_interval_sec, up to max_retries).
class Commander {
public:
    explicit Commander(const std::string& path,
                       std::uint64_t timeout_ns = 5'000'000'000,
                       int retry_interval_sec = 10,
                       int max_retries = 6);

    void setTimeoutHandler(CommandTimeoutFn handler) { timeout_handler_ = std::move(handler); }

    bool isConnected() const { return appender_ != nullptr; }
    const std::string& path() const { return path_; }
    bool hasCommandInFlight() const { return command_in_flight_; }

    template<typename CommandT>
    bool send(const CommandT& cmd, std::uint64_t current_time_ns) {
        if (!appender_ || command_in_flight_) return false;

        buffer_.reset();

        // TYPE_ID travels in the queue's message header, not the payload.
        cmd.encode(buffer_);
        appender_->write(buffer_, CommandT::TYPE_ID);

        command_in_flight_ = true;
        command_sent_time_ = current_time_ns;
        commands_sent_++;

        return true;
    }

    bool sendRaw(const zbuffer::ZBuffer& buf, std::uint32_t type_id, std::uint64_t current_time_ns) {
        if (!appender_ || command_in_flight_) return false;

        appender_->write(buf, type_id);

        command_in_flight_ = true;
        command_sent_time_ = current_time_ns;
        commands_sent_++;

        return true;
    }

    void onAck() {
        if (command_in_flight_) {
            command_in_flight_ = false;
            command_sent_time_ = 0;
            commands_acked_++;
        }
    }

    void checkTimeout(std::uint64_t current_time_ns) {
        if (!command_in_flight_) return;

        std::uint64_t elapsed = current_time_ns - command_sent_time_;
        if (elapsed > timeout_ns_) {
            command_timeouts_++;
            if (timeout_handler_) timeout_handler_(current_time_ns, elapsed);
        }
    }

    chronicle::Appender& appender() { return *appender_; }

    std::uint64_t commandsSent() const { return commands_sent_; }
    std::uint64_t commandsAcked() const { return commands_acked_; }
    std::uint64_t commandTimeouts() const { return command_timeouts_; }

private:
    std::string path_;
    std::unique_ptr<chronicle::Appender> appender_;
    zbuffer::ZBuffer buffer_;

    std::uint64_t timeout_ns_;
    CommandTimeoutFn timeout_handler_{defaultCommandTimeoutHandler};

    bool command_in_flight_{false};
    std::uint64_t command_sent_time_{0};

    std::uint64_t commands_sent_{0};
    std::uint64_t commands_acked_{0};
    std::uint64_t command_timeouts_{0};
};

} // namespace apps

#endif // APPS_COMMANDER_HPP