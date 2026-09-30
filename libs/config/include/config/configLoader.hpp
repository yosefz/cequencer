#ifndef CONFIG_LOADER_HPP
#define CONFIG_LOADER_HPP

#include "config.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace config {

/**
 * Configuration file format (simple key=value):
 *
 *   # Comment
 *   name = my_sequencer
 *   base_path = /var/data/sequencer
 *   max_command_size = 4096
 *   poll_sleep_us = 0
 *   clock_source = system
 *   apps = app1, app2, app3
 *
 * Whitespace around '=' is trimmed. Lines starting with '#' are comments.
 * List values (apps) are comma-separated.
 */

/**
 * Raw key-value store from config file.
 */
using ConfigValues = std::unordered_map<std::string, std::string>;

/**
 * Configuration loader.
 */
class ConfigLoader {
public:
    /**
     * Parse config file into raw key-value pairs.
     */
    static ConfigValues loadRaw(const std::filesystem::path& path);
    static ConfigValues parseRaw(const std::string& content);

    /**
     * Load sequencer configuration.
     */
    static SequencerConfig loadSequencerConfig(const std::filesystem::path& path);
    static SequencerConfig loadSequencerConfig(const ConfigValues& values);

    /**
     * Load stream app configuration.
     */
    static StreamAppConfig loadStreamAppConfig(const std::filesystem::path& path);
    static StreamAppConfig loadStreamAppConfig(const ConfigValues& values);

    /**
     * Load event stream app configuration (alias for loadStreamAppConfig).
     */
    static EventStreamAppConfig loadEventStreamAppConfig(const std::filesystem::path& path);
    static EventStreamAppConfig loadEventStreamAppConfig(const ConfigValues& values);

    /**
     * Save configuration to file.
     */
    static void save(const SequencerConfig& config, const std::filesystem::path& path);

    /**
     * Generate config file content.
     */
    static std::string serialize(const SequencerConfig& config);

    // Utility functions
    static std::string trim(const std::string& str);
    static std::string toLower(std::string str);
    static std::vector<std::string> split(const std::string& str, char delim);
    static ClockSource parseClockSource(const std::string& value);
    static std::string clockSourceToString(ClockSource source);
};

} // namespace config

#endif // CONFIG_LOADER_HPP