#ifndef SEQUENCER_COMMAND_SYSTEM_HPP
#define SEQUENCER_COMMAND_SYSTEM_HPP

#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sequencer {

class CommandHandler {
public:
    virtual ~CommandHandler() = default;

    // command views the queue mapping directly and is valid only for the
    // duration of this call.
    virtual bool onCommand(const std::string& app, zbuffer::ZBufferView& command) = 0;
};

struct VerificationResult {
    bool valid{true};
    const char* error{nullptr};
};

// Tails command queues from all apps, verifies sequence numbers, and dispatches to registered handlers.
class CommandSystem {
public:
    static constexpr std::size_t MAX_TYPE_IDS = 256;
    static constexpr std::size_t TYPE_ID_SIZE = 4;
    static constexpr std::size_t SEQUENCE_NUMBER_OFFSET = 2;

    explicit CommandSystem(std::size_t max_command_size);

    // Has virtual members; destroyed through this type by Sequencer.
    virtual ~CommandSystem() = default;

    CommandSystem(const CommandSystem&) = delete;
    CommandSystem& operator=(const CommandSystem&) = delete;
    CommandSystem(CommandSystem&&) = delete;
    CommandSystem& operator=(CommandSystem&&) = delete;

    void addQueue(const std::string& app_name, const std::string& path, std::size_t queue_size);

    // Handler must outlive CommandSystem. type_id must be < MAX_TYPE_IDS.
    void registerHandler(std::uint32_t type_id, CommandHandler& handler);

    std::size_t poll();

    struct Stats {
        std::uint64_t commands_processed{0};
        std::uint64_t verification_failures{0};
        std::uint64_t unknown_type_ids{0};
        std::uint64_t handler_errors{0};
        std::uint64_t poll_cycles{0};
        std::uint64_t empty_polls{0};
    };

    const Stats& stats() const noexcept { return stats_; }

protected:
    // Default: verifies seq_num == last_seq_num + 1. Override for custom logic.
    virtual VerificationResult verify(
        const std::string& app_name,
        std::uint32_t type_id,
        std::uint64_t seq_num,
        std::uint64_t last_seq_num,
        zbuffer::ZBufferView& buffer) noexcept;

private:
    struct CommandQueue {
        std::string app_name;
        std::unique_ptr<chronicle::Tailer> tailer;
        std::uint64_t last_seq_num{0};
    };

    bool processQueue(CommandQueue& queue);
    bool processCommand(CommandQueue& queue, const chronicle::MessageView& msg);

    std::vector<CommandQueue> queues_;
    std::array<CommandHandler*, MAX_TYPE_IDS> handlers_{};  // O(1) lookup, no allocation
    std::size_t max_command_size_;
    Stats stats_;
};

} // namespace sequencer

#endif // SEQUENCER_COMMAND_SYSTEM_HPP