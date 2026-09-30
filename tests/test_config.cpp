/**
 * Tests for config library.
 */
#include <config/config.hpp>
#include <config/configLoader.hpp>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// Test helper to create temp files
class TempFile {
public:
    TempFile(const std::string& content) {
        path_ = fs::temp_directory_path() / ("test_config_" + std::to_string(counter_++) + ".conf");
        std::ofstream f(path_);
        f << content;
    }
    ~TempFile() {
        fs::remove(path_);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static int counter_;
};
int TempFile::counter_ = 0;

void test_parse_raw() {
    std::cout << "test_parse_raw... ";

    TempFile f(R"(
# Comment line
name = test_app
base_path = /var/data

# Another comment
max_command_size = 4096
poll_sleep_us=500
)");

    auto values = config::ConfigLoader::loadRaw(f.path());

    assert(values.size() == 4);
    assert(values["name"] == "test_app");
    assert(values["base_path"] == "/var/data");
    assert(values["max_command_size"] == "4096");
    assert(values["poll_sleep_us"] == "500");

    std::cout << "PASSED\n";
}

void test_sequencer_config() {
    std::cout << "test_sequencer_config... ";

    TempFile f(R"(
name = my_sequencer
base_path = /tmp/seq_test
max_command_size = 8192
command_queue_size = 33554432
event_stream_size = 134217728
poll_sleep_us = 100
clock_source = steady
time_event_interval_ns = 1000000000
apps = gateway,risk,oms
)");

    auto config = config::ConfigLoader::loadSequencerConfig(f.path());

    assert(config.name == "my_sequencer");
    assert(config.base_path == "/tmp/seq_test");
    assert(config.max_command_size == 8192);
    assert(config.command_queue_size == 33554432);
    assert(config.event_stream_size == 134217728);
    assert(config.poll_sleep_us == 100);
    assert(config.clock_source == config::ClockSource::STEADY);
    assert(config.time_event_interval_ns == 1000000000);
    assert(config.apps.size() == 3);
    assert(config.apps[0] == "gateway");
    assert(config.apps[1] == "risk");
    assert(config.apps[2] == "oms");

    // Test derived paths
    assert(config.commands_path() == "/tmp/seq_test/commands");
    assert(config.events_path() == "/tmp/seq_test/events");
    assert(config.event_stream_file() == "/tmp/seq_test/events/my_sequencer.dat");
    assert(config.command_queue_file("gateway") == "/tmp/seq_test/commands/gateway.dat");

    std::cout << "PASSED\n";
}

void test_stream_app_config() {
    std::cout << "test_stream_app_config... ";

    TempFile f(R"(
name = trading_engine
base_path = /tmp/app_test
event_stream_path = /tmp/seq/events/events.dat
command_queue_path = /tmp/seq/commands/trading_engine.dat
start_from_beginning = true
poll_sleep_us = 50
clock_source = highres
)");

    auto config = config::ConfigLoader::loadStreamAppConfig(f.path());

    assert(config.name == "trading_engine");
    assert(config.base_path == "/tmp/app_test");
    assert(config.event_stream_path == "/tmp/seq/events/events.dat");
    assert(config.command_queue_path == "/tmp/seq/commands/trading_engine.dat");
    assert(config.start_from_beginning == true);
    assert(config.poll_sleep_us == 50);
    assert(config.clock_source == config::ClockSource::HIGH_RES);

    std::cout << "PASSED\n";
}

void test_clock_source_parsing() {
    std::cout << "test_clock_source_parsing... ";

    // Test system clock (default)
    {
        TempFile f("name=test\nclock_source=system");
        auto config = config::ConfigLoader::loadStreamAppConfig(f.path());
        assert(config.clock_source == config::ClockSource::SYSTEM);
    }

    // Test steady clock
    {
        TempFile f("name=test\nclock_source=steady");
        auto config = config::ConfigLoader::loadStreamAppConfig(f.path());
        assert(config.clock_source == config::ClockSource::STEADY);
    }

    // Test highres clock
    {
        TempFile f("name=test\nclock_source=highres");
        auto config = config::ConfigLoader::loadStreamAppConfig(f.path());
        assert(config.clock_source == config::ClockSource::HIGH_RES);
    }

    std::cout << "PASSED\n";
}

void test_boolean_parsing() {
    std::cout << "test_boolean_parsing... ";

    // Test true values
    for (const auto& val : {"true", "TRUE", "True", "1", "yes", "YES"}) {
        TempFile f(std::string("name=test\nstart_from_beginning=") + val);
        auto config = config::ConfigLoader::loadStreamAppConfig(f.path());
        assert(config.start_from_beginning == true);
    }

    // Test false values
    for (const auto& val : {"false", "FALSE", "0", "no", "NO"}) {
        TempFile f(std::string("name=test\nstart_from_beginning=") + val);
        auto config = config::ConfigLoader::loadStreamAppConfig(f.path());
        assert(config.start_from_beginning == false);
    }

    std::cout << "PASSED\n";
}

void test_defaults() {
    std::cout << "test_defaults... ";

    // Minimal config should use defaults
    TempFile f("name=minimal");
    auto config = config::ConfigLoader::loadSequencerConfig(f.path());

    assert(config.name == "minimal");
    assert(config.max_command_size == 4096);  // Default
    assert(config.command_queue_size == 64 * 1024 * 1024);  // Default
    assert(config.event_stream_size == 256 * 1024 * 1024);  // Default
    assert(config.poll_sleep_us == 0);  // Default (busy spin)  <-- Changed from 1000
    assert(config.clock_source == config::ClockSource::SYSTEM);  // Default

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Config Tests ===\n\n";

    test_parse_raw();
    test_sequencer_config();
    test_stream_app_config();
    test_clock_source_parsing();
    test_boolean_parsing();
    test_defaults();

    std::cout << "\nAll config tests passed!\n";
    return 0;
}