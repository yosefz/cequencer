/**
 * Tests for apps library.
 */
#include <apps/all.hpp>
#include <sequencer/sequencer.hpp>
#include <sequencer/eventSystem.hpp>
#include <sequencer/commandSystem.hpp>
#include <config/config.hpp>
#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

// Test fixture that cleans up after itself
class TestFixture {
public:
    TestFixture(const std::string& name)
        : base_path_(fs::temp_directory_path() / ("test_apps_" + name + "_" + std::to_string(counter_++))) {
        fs::remove_all(base_path_);  // a previous aborted run may have left data behind
        fs::create_directories(base_path_);
    }

    ~TestFixture() {
        fs::remove_all(base_path_);
    }

    fs::path base_path() const { return base_path_; }

    config::SequencerConfig makeSequencerConfig(const std::vector<std::string>& apps = {"test_app"}) {
        config::SequencerConfig config;
        config.name = "test_sequencer";
        config.base_path = base_path_.string();
        config.max_command_size = 1024;
        config.max_event_size = 1024;
        config.command_queue_size = 1024 * 1024;
        config.event_stream_size = 1024 * 1024;
        config.poll_sleep_us = 0;
        config.clock_source = config::ClockSource::STEADY;
        config.apps = apps;
        return config;
    }

    config::StreamAppConfig makeStreamAppConfig(const std::string& app_name) {
        config::StreamAppConfig config;
        config.name = app_name;
        config.base_path = (base_path_ / app_name).string();
        config.event_stream_path = (base_path_ / "events" / "test_sequencer.dat").string();
        config.command_queue_path = (base_path_ / "commands" / (app_name + ".dat")).string();
        config.start_from_beginning = true;
        config.poll_sleep_us = 0;
        config.clock_source = config::ClockSource::STEADY;
        return config;
    }

private:
    fs::path base_path_;
    static int counter_;
};
int TestFixture::counter_ = 0;

// Test encoder/decoder matching new codegen pattern
class TestCommandEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 100;
    static constexpr std::size_t SIZE = 8;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        buf_ = &buffer;
        offset_ = buffer.write_pos();
    }

    void commit() noexcept {
        if (buf_) buf_->advance_write(SIZE);
    }

    std::byte* data() noexcept {
        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;
    }

    void setValue(std::uint64_t v) noexcept {
        std::memcpy(data(), &v, sizeof(v));
    }

private:
    zbuffer::ZBuffer* buf_{nullptr};
    std::size_t offset_{0};
};

class TestEventEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 200;
    static constexpr std::size_t SIZE = 16;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        buf_ = &buffer;
        offset_ = buffer.write_pos();
    }

    void commit() noexcept {
        if (buf_) buf_->advance_write(SIZE);
    }

    std::byte* data() noexcept {
        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;
    }

    void setSequence(std::uint64_t v) noexcept {
        std::memcpy(data(), &v, sizeof(v));
    }

    void setData(std::uint64_t v) noexcept {
        std::memcpy(data() + 8, &v, sizeof(v));
    }

private:
    zbuffer::ZBuffer* buf_{nullptr};
    std::size_t offset_{0};
};

struct TestEvent {
    static constexpr std::uint32_t TYPE_ID = 200;
    std::uint64_t sequence;
    std::uint64_t data;

    static TestEvent decode(const std::byte* data) {
        TestEvent evt;
        std::memcpy(&evt.sequence, data, sizeof(evt.sequence));
        std::memcpy(&evt.data, data + sizeof(evt.sequence), sizeof(evt.data));
        return evt;
    }
};

// System events for testing
struct SysTimeEvent {
    static constexpr std::uint32_t TYPE_ID = 1;
    std::uint64_t timestamp;

    static SysTimeEvent decode(const std::byte* data) {
        SysTimeEvent e;
        std::memcpy(&e.timestamp, data, sizeof(e.timestamp));
        return e;
    }
};

class SysTimeEventEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 1;
    static constexpr std::size_t SIZE = 8;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        buf_ = &buffer;
        offset_ = buffer.write_pos();
    }

    void commit() noexcept {
        if (buf_) buf_->advance_write(SIZE);
    }

    std::byte* data() noexcept {
        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;
    }

    void setTimestamp(std::uint64_t v) noexcept {
        std::memcpy(data(), &v, sizeof(v));
    }

private:
    zbuffer::ZBuffer* buf_{nullptr};
    std::size_t offset_{0};
};

struct SysStartOfSessionEvent {
    static constexpr std::uint32_t TYPE_ID = 2;
    char session_id[32];

    static SysStartOfSessionEvent decode(const std::byte* data) {
        SysStartOfSessionEvent e;
        std::memcpy(e.session_id, data, 32);
        return e;
    }
};

class SysStartOfSessionEventEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 2;
    static constexpr std::size_t SIZE = 32;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        buf_ = &buffer;
        offset_ = buffer.write_pos();
    }

    void commit() noexcept {
        if (buf_) buf_->advance_write(SIZE);
    }

    std::byte* data() noexcept {
        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;
    }

    void setSessionId(const char* v) noexcept {
        std::memset(data(), 0, 32);
        std::strncpy(reinterpret_cast<char*>(data()), v, 31);
    }

private:
    zbuffer::ZBuffer* buf_{nullptr};
    std::size_t offset_{0};
};

//------------------------------------------------------------------------------
// Pollable Tests
//------------------------------------------------------------------------------

class TestPollable : public apps::Pollable {
public:
    std::size_t pollOne(std::uint64_t nanos) override {
        last_poll_time = nanos;
        poll_count++;
        return 1;
    }

    std::uint64_t last_poll_time{0};
    int poll_count{0};
};

void test_pollable() {
    std::cout << "test_pollable... ";

    TestPollable pollable;

    assert(pollable.poll_count == 0);

    auto result = pollable.pollOne(12345);
    assert(result == 1);
    assert(pollable.poll_count == 1);
    assert(pollable.last_poll_time == 12345);

    std::cout << "PASSED\n";
}

//------------------------------------------------------------------------------
// Commander Tests
//------------------------------------------------------------------------------

void test_commander_connects() {
    std::cout << "test_commander_connects... ";

    TestFixture fixture("commander");
    auto seq_config = fixture.makeSequencerConfig({"my_app"});

    // Initialize sequencer to create command queue
    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    // Commander should connect immediately
    apps::Commander commander(seq_config.command_queue_file("my_app"), 1, 0);

    assert(commander.isConnected());
    assert(commander.path() == seq_config.command_queue_file("my_app"));

    std::cout << "PASSED\n";
}

void test_commander_send() {
    std::cout << "test_commander_send... ";

    TestFixture fixture("commander_send");
    auto seq_config = fixture.makeSequencerConfig({"my_app"});

    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    // Use longer timeout, no retries for test
    apps::Commander commander(seq_config.command_queue_file("my_app"), 5'000'000'000, 1, 0);

    // Send commands using encoder pattern
    zbuffer::ZBuffer buf(256);
    std::uint64_t time = 1000;

    for (int i = 1; i <= 3; i++) {
        buf.reset();

        TestCommandEncoder encoder;
        encoder.wrap(buf);
        encoder.setValue(i * 100);
        encoder.commit();

        assert(commander.sendRaw(buf, TestCommandEncoder::TYPE_ID, time++));
        commander.onAck();
    }

    assert(commander.commandsSent() == 3);

    // Read back via tailer
    auto tailer = chronicle::Tailer::open(seq_config.command_queue_file("my_app"));

    int count = 0;
    while (auto view = tailer.poll()) {
        assert(view.type_id() == TestCommandEncoder::TYPE_ID);

        std::uint64_t value;
        std::memcpy(&value, view.data(), sizeof(value));
        count++;
        assert(value == static_cast<std::uint64_t>(count * 100));
    }

    assert(count == 3);

    std::cout << "PASSED\n";
}

void test_commander_retry_fails() {
    std::cout << "test_commander_retry_fails... ";

    // Try to connect to non-existent queue with fast retries
    try {
        apps::Commander commander("/nonexistent/path/queue.dat", 0, 2);  // 0 sec retry, 2 attempts
        assert(false && "Should have thrown");
    } catch (const std::runtime_error& e) {
        // Expected
        std::string msg = e.what();
        assert(msg.find("not found") != std::string::npos);
    }

    std::cout << "PASSED\n";
}

//------------------------------------------------------------------------------
// StreamApp Tests (using test subclass)
//------------------------------------------------------------------------------

class TestStreamApp : public apps::StreamApp {
public:
    TestStreamApp(const std::string& name, const std::string& base_path)
        : StreamApp(name, base_path, config::ClockSource::STEADY) {}

    void onRunOne(std::uint64_t nanos) override {
        last_nanos = nanos;
        run_count++;
        if (run_count >= max_runs) {
            stop();
        }
    }

    void onInitialize() override {
        initialized = true;
    }

    void onRun() override {
        while (isRunning()) {
            onRunOne(clock().now());
        }
    }

    void onShutdown() override {
        shutdown_called = true;
    }

    // Expose protected methods for testing
    void testSetCurrentTime(std::uint64_t nanos) { setCurrentTime(nanos); }
    void testMarkSessionStarted(const std::string& id) { markSessionStarted(id); }

    bool initialized{false};
    bool shutdown_called{false};
    std::uint64_t last_nanos{0};
    int run_count{0};
    int max_runs{5};
};

void test_stream_app_lifecycle() {
    std::cout << "test_stream_app_lifecycle... ";

    TestFixture fixture("streamapp");

    TestStreamApp app("test", fixture.base_path().string());
    app.max_runs = 3;

    assert(!app.initialized);
    assert(!app.isRunning());

    app.initialize();
    assert(app.initialized);

    app.run();

    assert(app.run_count == 3);
    assert(app.shutdown_called);
    assert(!app.isRunning());

    std::cout << "PASSED\n";
}

void test_stream_app_time_tracking() {
    std::cout << "test_stream_app_time_tracking... ";

    TestFixture fixture("time");

    TestStreamApp app("test", fixture.base_path().string());

    assert(app.currentTime() == 0);
    assert(!app.isSessionStarted());

    // Simulate receiving events
    app.testSetCurrentTime(1000000000);  // 1 second
    assert(app.currentTime() == 1000000000);

    app.testMarkSessionStarted("SESSION-001");
    assert(app.isSessionStarted());
    assert(app.sessionId() == "SESSION-001");

    std::cout << "PASSED\n";
}

//------------------------------------------------------------------------------
// EventStreamApp Tests
//------------------------------------------------------------------------------

class TestEventStreamApp : public apps::EventStreamApp {
public:
    using EventStreamApp::EventStreamApp;

    void onEvent(std::uint64_t nanos, std::uint32_t type_id,
                 const std::byte* data, std::size_t length) override {
        events_received++;
        last_type_id = type_id;
        last_nanos = nanos;

        if (type_id == TestEvent::TYPE_ID && length >= sizeof(TestEvent::sequence) + sizeof(TestEvent::data)) {
            auto evt = TestEvent::decode(data);
            last_event_data = evt.data;
        }
    }

    // Expose protected methods for testing
    std::size_t testPollEvents() { return pollEvents(); }
    void testOnRunOne(std::uint64_t nanos) { onRunOne(nanos); }

    int events_received{0};
    std::uint32_t last_type_id{0};
    std::uint64_t last_nanos{0};
    std::uint64_t last_event_data{0};
};

// Helper to write events to event stream using EventSystem pattern
void writeTestEvent(sequencer::EventSystem& events, std::uint64_t seq, std::uint64_t data) {
    TestEventEncoder encoder;
    encoder.wrap(events.buffer());
    encoder.setSequence(seq);
    encoder.setData(data);
    events.commitEventRaw(encoder);
}

void test_event_stream_app_receives_events() {
    std::cout << "test_event_stream_app_receives_events... ";

    TestFixture fixture("event_app");
    auto seq_config = fixture.makeSequencerConfig({"test_app"});

    // Initialize sequencer
    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    // Write some events using EventSystem
    auto& events = seq.eventSystem();
    writeTestEvent(events, 1, 100);
    writeTestEvent(events, 2, 200);
    writeTestEvent(events, 3, 300);

    // Create app config
    auto app_config = fixture.makeStreamAppConfig("test_app");

    // Create and run app
    TestEventStreamApp app(app_config);
    app.initialize();

    // Poll events (one iteration)
    auto processed = app.testPollEvents();

    assert(processed == 3);
    assert(app.events_received == 3);
    assert(app.last_type_id == TestEvent::TYPE_ID);
    assert(app.last_event_data == 300);

    std::cout << "PASSED\n";
}

void test_event_stream_app_with_pollables() {
    std::cout << "test_event_stream_app_with_pollables... ";

    TestFixture fixture("pollables");
    auto seq_config = fixture.makeSequencerConfig({"test_app"});

    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    auto app_config = fixture.makeStreamAppConfig("test_app");
    TestEventStreamApp app(app_config);

    TestPollable pollable1, pollable2;
    app.addPollable(&pollable1);
    app.addPollable(&pollable2);

    app.initialize();

    // Run one iteration
    app.testOnRunOne(12345);

    assert(pollable1.poll_count == 1);
    assert(pollable2.poll_count == 1);
    assert(pollable1.last_poll_time == 12345);

    // Remove one pollable
    app.removePollable(&pollable1);

    app.testOnRunOne(67890);

    assert(pollable1.poll_count == 1);  // Not polled again
    assert(pollable2.poll_count == 2);

    std::cout << "PASSED\n";
}

void test_event_stream_app_system_events() {
    std::cout << "test_event_stream_app_system_events... ";

    TestFixture fixture("sysevents");
    auto seq_config = fixture.makeSequencerConfig({"test_app"});

    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    auto& events = seq.eventSystem();

    // Write system events using encoder pattern
    {
        SysStartOfSessionEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setSessionId("TEST-SESSION");
        events.commitEventRaw(encoder);
    }

    {
        SysTimeEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setTimestamp(999000000);
        events.commitEventRaw(encoder);
    }

    // Write a business event
    writeTestEvent(events, 1, 42);

    // Create app with system event handler
    auto app_config = fixture.makeStreamAppConfig("test_app");
    TestEventStreamApp app(app_config);

    app.setSystemEventHandler([](std::uint32_t type_id, const std::byte* data, std::size_t len)
        -> apps::SystemEventResult {
        (void)len;
        if (type_id == SysTimeEvent::TYPE_ID) {
            auto e = SysTimeEvent::decode(data);
            return {.kind = apps::SystemEventKind::Handled, .timestamp = e.timestamp};
        }
        if (type_id == SysStartOfSessionEvent::TYPE_ID) {
            // string_view into the event buffer, which outlives this call
            const char* p = reinterpret_cast<const char*>(data);
            return {.kind = apps::SystemEventKind::SessionStart,
                    .session_id = std::string_view(p, strnlen(p, 32))};
        }
        return {};
    });

    app.initialize();
    app.testPollEvents();

    // Check system events were processed
    assert(app.isSessionStarted());
    assert(app.sessionId() == "TEST-SESSION");
    assert(app.currentTime() == 999000000);

    // Check only business event went to onEvent
    assert(app.events_received == 1);
    assert(app.last_type_id == TestEvent::TYPE_ID);

    // Check stats
    assert(app.stats().system_events == 2);
    assert(app.stats().events_processed == 3);  // Total events

    std::cout << "PASSED\n";
}

void test_event_stream_app_send_command() {
    std::cout << "test_event_stream_app_send_command... ";

    TestFixture fixture("sendcmd");
    auto seq_config = fixture.makeSequencerConfig({"test_app"});

    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    auto app_config = fixture.makeStreamAppConfig("test_app");
    TestEventStreamApp app(app_config);

    app.initialize();

    assert(app.hasCommander());

    // Send commands using raw buffer (ack between sends due to one-in-flight)
    zbuffer::ZBuffer buf(256);
    std::uint64_t time = 1000;

    for (int i = 0; i < 2; i++) {
        buf.reset();

        TestCommandEncoder encoder;
        encoder.wrap(buf);
        encoder.setValue((i + 1) * 111);
        encoder.commit();

        assert(app.sendCommandRaw(buf, TestCommandEncoder::TYPE_ID, time++));
        app.commander()->onAck();
    }

    assert(app.commander()->commandsSent() == 2);

    // Verify commands in queue
    auto tailer = chronicle::Tailer::open(seq_config.command_queue_file("test_app"));

    int count = 0;
    while (auto view = tailer.poll()) {
        assert(view.type_id() == TestCommandEncoder::TYPE_ID);
        count++;
    }

    assert(count == 2);

    std::cout << "PASSED\n";
}

//------------------------------------------------------------------------------
// Integration Test
//------------------------------------------------------------------------------

void test_integration_roundtrip() {
    std::cout << "test_integration_roundtrip... ";

    TestFixture fixture("integration");
    auto seq_config = fixture.makeSequencerConfig({"trading_app"});

    // Set up sequencer
    sequencer::Sequencer seq(seq_config);
    seq.initialize();

    // Set up handler that echoes commands as events
    class EchoHandler : public sequencer::CommandHandler {
    public:
        EchoHandler(sequencer::EventSystem& e) : events(e) {}

        bool onCommand(const std::string& app, zbuffer::ZBufferView& buffer) override {
            (void)app;

            // Buffer is already positioned after TYPE_ID
            std::uint64_t value;
            std::memcpy(&value, buffer.read_ptr(), sizeof(value));
            buffer.skip(sizeof(value));

            TestEventEncoder encoder;
            encoder.wrap(events.buffer());
            encoder.setSequence(events.nextSequence());
            encoder.setData(value * 2);  // Echo with doubled value
            events.commitEventRaw(encoder);
            return true;
        }

        sequencer::EventSystem& events;
    };

    EchoHandler handler(seq.eventSystem());
    seq.commandSystem().registerHandler(TestCommandEncoder::TYPE_ID, handler);

    // Set up app
    auto app_config = fixture.makeStreamAppConfig("trading_app");
    TestEventStreamApp app(app_config);
    app.initialize();

    // App sends commands
    zbuffer::ZBuffer buf(256);
    std::uint64_t time = 1000;

    for (int value : {50, 100, 150}) {
        buf.reset();

        TestCommandEncoder encoder;
        encoder.wrap(buf);
        encoder.setValue(value);
        encoder.commit();

        assert(app.sendCommandRaw(buf, TestCommandEncoder::TYPE_ID, time++));
        app.commander()->onAck();
    }

    // Sequencer processes commands
    auto processed = seq.pollCommands();
    assert(processed == 3);
    assert(seq.eventSystem().currentSequence() == 3);

    // App receives events
    app.testPollEvents();
    assert(app.events_received == 3);
    assert(app.last_event_data == 300);  // 150 * 2

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Apps Tests ===\n\n";

    // Pollable tests
    test_pollable();

    // Commander tests
    test_commander_connects();
    test_commander_send();
    test_commander_retry_fails();

    // StreamApp tests
    test_stream_app_lifecycle();
    test_stream_app_time_tracking();

    // EventStreamApp tests
    test_event_stream_app_receives_events();
    test_event_stream_app_with_pollables();
    test_event_stream_app_system_events();
    test_event_stream_app_send_command();

    // Integration tests
    test_integration_roundtrip();

    std::cout << "\nAll apps tests passed!\n";
    return 0;
}