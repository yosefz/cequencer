#include "config/configLoader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace config {

std::string ConfigLoader::trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ConfigLoader::toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return str;
}

std::vector<std::string> ConfigLoader::split(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        auto trimmed = trim(item);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

ClockSource ConfigLoader::parseClockSource(const std::string& value) {
    auto lower = toLower(value);
    if (lower == "system") return ClockSource::SYSTEM;
    if (lower == "steady") return ClockSource::STEADY;
    if (lower == "high_res" || lower == "highres") return ClockSource::HIGH_RES;
    throw std::runtime_error("Unknown clock source: " + value);
}

std::string ConfigLoader::clockSourceToString(ClockSource source) {
    switch (source) {
        case ClockSource::SYSTEM: return "system";
        case ClockSource::STEADY: return "steady";
        case ClockSource::HIGH_RES: return "high_res";
    }
    return "system";
}

ConfigValues ConfigLoader::loadRaw(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open config file: " + path.string());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseRaw(buffer.str());
}

ConfigValues ConfigLoader::parseRaw(const std::string& content) {
    ConfigValues values;
    
    std::istringstream stream(content);
    std::string line;
    int lineNum = 0;
    
    while (std::getline(stream, line)) {
        lineNum++;
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse key=value
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) {
            throw std::runtime_error("Invalid config line " + std::to_string(lineNum) + 
                                     ": missing '='");
        }
        
        auto key = toLower(trim(line.substr(0, eqPos)));
        auto value = trim(line.substr(eqPos + 1));
        values[key] = value;
    }
    
    return values;
}

SequencerConfig ConfigLoader::loadSequencerConfig(const std::filesystem::path& path) {
    return loadSequencerConfig(loadRaw(path));
}

SequencerConfig ConfigLoader::loadSequencerConfig(const ConfigValues& values) {
    SequencerConfig config;
    
    // Base config
    if (values.count("name")) {
        config.name = values.at("name");
    }
    if (values.count("base_path")) {
        config.base_path = values.at("base_path");
    }
    
    // Sequencer-specific
    if (values.count("max_command_size")) {
        config.max_command_size = std::stoull(values.at("max_command_size"));
    }
    if (values.count("max_event_size")) {
        config.max_event_size = std::stoull(values.at("max_event_size"));
    }
    if (values.count("command_queue_size")) {
        config.command_queue_size = std::stoull(values.at("command_queue_size"));
    }
    if (values.count("event_stream_size")) {
        config.event_stream_size = std::stoull(values.at("event_stream_size"));
    }
    if (values.count("poll_sleep_us")) {
        config.poll_sleep_us = std::stoul(values.at("poll_sleep_us"));
    }
    if (values.count("clock_source")) {
        config.clock_source = parseClockSource(values.at("clock_source"));
    }
    if (values.count("time_event_interval_ns")) {
        config.time_event_interval_ns = std::stoull(values.at("time_event_interval_ns"));
    }
    if (values.count("apps")) {
        config.apps = split(values.at("apps"), ',');
    }
    
    return config;
}

StreamAppConfig ConfigLoader::loadStreamAppConfig(const std::filesystem::path& path) {
    return loadStreamAppConfig(loadRaw(path));
}

StreamAppConfig ConfigLoader::loadStreamAppConfig(const ConfigValues& values) {
    StreamAppConfig config;
    
    // Base config
    if (values.count("name")) {
        config.name = values.at("name");
    }
    if (values.count("base_path")) {
        config.base_path = values.at("base_path");
    }
    
    // Stream app specific
    if (values.count("event_stream_path")) {
        config.event_stream_path = values.at("event_stream_path");
    }
    if (values.count("command_queue_path")) {
        config.command_queue_path = values.at("command_queue_path");
    }
    if (values.count("start_from_beginning")) {
        auto val = toLower(values.at("start_from_beginning"));
        config.start_from_beginning = (val == "true" || val == "1" || val == "yes");
    }
    if (values.count("poll_sleep_us")) {
        config.poll_sleep_us = std::stoul(values.at("poll_sleep_us"));
    }
    if (values.count("clock_source")) {
        config.clock_source = parseClockSource(values.at("clock_source"));
    }
    
    return config;
}

EventStreamAppConfig ConfigLoader::loadEventStreamAppConfig(const std::filesystem::path& path) {
    return loadStreamAppConfig(path);
}

EventStreamAppConfig ConfigLoader::loadEventStreamAppConfig(const ConfigValues& values) {
    return loadStreamAppConfig(values);
}

void ConfigLoader::save(const SequencerConfig& config, const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot write config file: " + path.string());
    }
    file << serialize(config);
}

std::string ConfigLoader::serialize(const SequencerConfig& config) {
    std::ostringstream ss;
    
    ss << "# Sequencer Configuration\n";
    ss << "\n";
    ss << "# Application identity\n";
    ss << "name = " << config.name << "\n";
    ss << "\n";
    ss << "# Paths\n";
    ss << "base_path = " << config.base_path << "\n";
    ss << "\n";
    ss << "# Performance tuning\n";
    ss << "max_command_size = " << config.max_command_size << "\n";
    ss << "command_queue_size = " << config.command_queue_size << "\n";
    ss << "event_stream_size = " << config.event_stream_size << "\n";
    ss << "poll_sleep_us = " << config.poll_sleep_us << "  # 0 = busy spin\n";
    ss << "\n";
    ss << "# Clock source: system, steady, high_res\n";
    ss << "clock_source = " << clockSourceToString(config.clock_source) << "\n";
    ss << "\n";
    ss << "# Connected applications (comma-separated)\n";
    ss << "apps = ";
    for (std::size_t i = 0; i < config.apps.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << config.apps[i];
    }
    ss << "\n";
    
    return ss.str();
}

} // namespace config