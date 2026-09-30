#include "sequencer/commandSystem.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace sequencer {

CommandSystem::CommandSystem(std::size_t max_command_size)
    : max_command_size_(max_command_size) {
    handlers_.fill(nullptr);
}

void CommandSystem::addQueue(const std::string& app_name, const std::string& path, std::size_t queue_size) {
    // Create queue file if it doesn't exist
    if (!std::filesystem::exists(path)) {
        auto appender = chronicle::Appender::create(path, queue_size);
    }
    
    CommandQueue queue;
    queue.app_name = app_name;
    queue.tailer = std::make_unique<chronicle::Tailer>(
        chronicle::Tailer::open(path, false));
    queue.last_seq_num = 0;
    
    queues_.push_back(std::move(queue));
    std::cout << "  CommandSystem: added queue for " << app_name << ": " << path << "\n";
}

void CommandSystem::registerHandler(std::uint32_t type_id, CommandHandler& handler) {
    if (type_id >= MAX_TYPE_IDS) {
        throw std::out_of_range("type_id exceeds MAX_TYPE_IDS");
    }
    handlers_[type_id] = &handler;
}

std::size_t CommandSystem::poll() {
    stats_.poll_cycles++;
    std::size_t processed = 0;
    
    for (auto& queue : queues_) {
        while (processQueue(queue)) {
            processed++;
        }
    }
    
    if (processed == 0) {
        stats_.empty_polls++;
    }
    
    return processed;
}

bool CommandSystem::processQueue(CommandQueue& queue) {
    auto msg = queue.tailer->poll();
    if (!msg) {
        return false;
    }

    return processCommand(queue, msg);
}

bool CommandSystem::processCommand(CommandQueue& queue, const chronicle::MessageView& msg) {
    if (msg.length() > max_command_size_) [[unlikely]] {
        stats_.handler_errors++;
        std::cerr << "Command from " << queue.app_name << " exceeds max_command_size ("
                  << msg.length() << " > " << max_command_size_ << ")\n";
        return false;
    }

    // Reads straight out of the queue mapping - no copy, and read-only, so a
    // handler cannot write back into a PROT_READ page.
    zbuffer::ZBufferView buffer{msg.data(), msg.length()};

    // TYPE_ID travels in the message header, in the same cache line as the length
    const std::uint32_t type_id = msg.type_id();

    // Read sequence number from command (at offset 2 in command data, after messageVersion)
    std::uint64_t seq_num = 0;
    if (buffer.readable() >= SEQUENCE_NUMBER_OFFSET + sizeof(seq_num)) {
        std::memcpy(&seq_num, buffer.read_ptr() + SEQUENCE_NUMBER_OFFSET, sizeof(seq_num));
    }

    auto result = verify(queue.app_name, type_id, seq_num, queue.last_seq_num, buffer);
    if (!result.valid) {
        stats_.verification_failures++;
        std::cerr << "Command verification failed for " << queue.app_name 
                  << " [type=" << type_id << " seq=" << seq_num 
                  << " last=" << queue.last_seq_num << "]: " 
                  << (result.error ? result.error : "unknown") << "\n";
        return false;
    }
    
    // Update last sequence number
    queue.last_seq_num = seq_num;
    
    // Dispatch to handler
    if (type_id >= MAX_TYPE_IDS || handlers_[type_id] == nullptr) {
        stats_.unknown_type_ids++;
        std::cerr << "No handler for type_id " << type_id << "\n";
        return false;
    }
    
    if (!handlers_[type_id]->onCommand(queue.app_name, buffer)) {
        stats_.handler_errors++;
        return false;
    }
    
    stats_.commands_processed++;
    return true;
}

VerificationResult CommandSystem::verify(
    const std::string& app_name,
    std::uint32_t type_id,
    std::uint64_t seq_num,
    std::uint64_t last_seq_num,
    zbuffer::ZBufferView& buffer) noexcept {

    (void)app_name;
    (void)type_id;
    (void)buffer;
    
    // First command from app: accept any sequence number
    if (last_seq_num == 0) {
        return {true, nullptr};
    }
    
    // Verify sequence is exactly one greater
    if (seq_num != last_seq_num + 1) {
        return {false, "sequence number gap detected"};
    }
    
    return {true, nullptr};
}

} // namespace sequencer