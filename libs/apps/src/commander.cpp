#include "apps/commander.hpp"

#include <stdexcept>

namespace apps {

    Commander::Commander(const std::string& path,
                         std::uint64_t timeout_ns,
                         int retry_interval_sec,
                         int max_retries)
        : path_(path)
        , buffer_(4096)
        , timeout_ns_(timeout_ns) {

        int attempts = 0;

        while (attempts <= max_retries) {
            if (std::filesystem::exists(path)) {
                try {
                    // Open existing queue file for appending
                    appender_ = std::make_unique<chronicle::Appender>(
                        chronicle::Appender::create(path));
                    std::cout << "Commander connected to: " << path << "\n";
                    return;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to open command queue: " << e.what() << "\n";
                }
            }

            if (attempts < max_retries) {
                std::cout << "Command queue not found: " << path
                          << " (retry " << (attempts + 1) << "/" << max_retries
                          << " in " << retry_interval_sec << "s)\n";
                std::this_thread::sleep_for(std::chrono::seconds(retry_interval_sec));
            }

            attempts++;
        }

        throw std::runtime_error("Command queue not found after " +
                                 std::to_string(max_retries) + " retries: " + path);
    }

} // namespace apps