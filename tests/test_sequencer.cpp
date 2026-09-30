/**
 * Tests for sequencer core library.
 */
#include <sequencer/clock.hpp>
#include <sequencer/sequencer.hpp>
#include <sequencer/eventSystem.hpp>
#include <sequencer/commandSystem.hpp>
#include <config/config.hpp>
#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

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
        : base_path_(fs::temp_directory_path() / ("test_seq_" + name + "_" + std::to_string(counter_++))) {
        fs::remove_all(base_path_);  // a previous aborted run may have left data behind
        fs::create_directories(base_path_);
    }

    ~TestFixture() {
        fs::remove_all(base_path_);
    }

    fs::path base_path() const { return base_path_; }

    config::SequencerConfig makeConfig(const std::vector<std::string>& apps = {"test_app"}) {
        config::SequencerConfig config;
        config.name = "test_sequencer";
        config.base_path = base_path_.string();
        config.max_command_size = 1024;
        config.max_event_size = 1024;
        config.command_queue_size = 1024 * 1024;  // 1MB
        config.event_stream_size = 1024 * 1024;   // 1MB
        config.poll_sleep_us = 0;
        config.clock_source = config::ClockSource::STEADY;
        config.apps = apps;
        return config;
    }

private:
    fs::path base_path_;
    static int counter_;
};
int TestFixture::counter_ = 0;

//------------------------------------------------------------------------------
// Clock Tests
//------------------------------------------------------------------------------

void test_system_clock() {
    std::cout << "test_system_clock... ";

    auto clock = sequencer::createClock(config::ClockSource::SYSTEM);

    auto t1 = clock->now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = clock->now();

    assert(t2 > t1);
    assert(t2 - t1 >= 10'000'000);  // At least 10ms in nanos

    std::cout << "PASSED\n";
}

void test_steady_clock() {
    std::cout << "test_steady_clock... ";

    auto clock = sequencer::createClock(config::ClockSource::STEADY);

    auto t1 = clock->now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = clock->now();

    assert(t2 > t1);
    assert(t2 - t1 >= 10'000'000);  // At least 10ms in nanos

    std::cout << "PASSED\n";
}

void test_highres_clock() {
    std::cout << "test_highres_clock... ";

    auto clock = sequencer::createClock(config::ClockSource::HIGH_RES);

    auto t1 = clock->now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = clock->now();

    assert(t2 > t1);
    assert(t2 - t1 >= 10'000'000);  // At least 10ms in nanos

    std::cout << "PASSED\n";
}

//------------------------------------------------------------------------------
// Sequencer Tests
//------------------------------------------------------------------------------

void test_sequencer_initialize() {
    std::cout << "test_sequencer_initialize... ";

    TestFixture fixture("init");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    // Check directories were created
    assert(fs::exists(config.commands_path()));
    assert(fs::exists(config.events_path()));

    // Check queues were created
    assert(fs::exists(config.command_queue_file("test_app")));
    assert(fs::exists(config.event_stream_file()));

    std::cout << "PASSED\n";
}

void test_sequencer_sequence_numbers() {
    std::cout << "test_sequencer_sequence_numbers... ";

    TestFixture fixture("seqnum");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    // Sequence numbers are now managed by EventSystem
    auto& events = seq.eventSystem();
    assert(events.currentSequence() == 0);
    assert(events.nextSequence() == 1);
    assert(events.nextSequence() == 2);
    assert(events.nextSequence() == 3);
    assert(events.currentSequence() == 3);

    std::cout << "PASSED\n";
}

void test_sequencer_time() {
    std::cout << "test_sequencer_time... ";

    TestFixture fixture("time");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    auto t1 = seq.now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto t2 = seq.now();

    assert(t2 > t1);

    std::cout << "PASSED\n";
}

// Simple test encoder/decoder matching new codegen pattern
class TestEventEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 200;
    static constexpr std::size_t SIZE = 16;  // sequence(8) + data(8)

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

class TestEventDecoder {
public:
    static constexpr std::uint32_t TYPE_ID = 200;
    static constexpr std::size_t SIZE = 16;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        view_ = buffer.sliceReadable(0, SIZE);
    }

    static void skip(zbuffer::ZBuffer& buffer) noexcept {
        buffer.skip(SIZE);
    }

    const std::byte* data() const noexcept {
        return reinterpret_cast<const std::byte*>(view_.data());
    }

    std::uint64_t getSequence() const noexcept {
        std::uint64_t v;
        std::memcpy(&v, data(), sizeof(v));
        return v;
    }

    std::uint64_t getData() const noexcept {
        std::uint64_t v;
        std::memcpy(&v, data() + 8, sizeof(v));
        return v;
    }

private:
    zbuffer::ZBuffer view_;
};

void test_event_system() {
    std::cout << "test_event_system... ";

    TestFixture fixture("evtsys");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    auto& events = seq.eventSystem();

    // Write some events using the EventSystem pattern
    for (int i = 1; i <= 3; i++) {
        TestEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setSequence(events.nextSequence());  // Manually setting for test
        encoder.setData(i * 100);
        events.commitEventRaw(encoder);  // Raw commit since we set seq manually
    }

    assert(events.currentSequence() == 3);

    // Read them back
    auto tailer = chronicle::Tailer::open(config.event_stream_file());

    int count = 0;
    while (auto view = tailer.poll()) {
        // TYPE_ID now travels in the message header, not the payload
        assert(view.length() == TestEventDecoder::SIZE);
        assert(view.type_id() == TestEventDecoder::TYPE_ID);

        zbuffer::ZBuffer buf(view.length());
        buf.write(view.data(), TestEventDecoder::SIZE);
        buf.set_read_pos(0);

        TestEventDecoder decoder;
        decoder.wrap(buf);

        count++;
        assert(decoder.getSequence() == static_cast<std::uint64_t>(count));
        assert(decoder.getData() == static_cast<std::uint64_t>(count * 100));
    }

    assert(count == 3);

    std::cout << "PASSED\n";
}

// Test handler using new CommandHandler interface
class TestHandler : public sequencer::CommandHandler {
public:
    static constexpr std::uint32_t TEST_CMD_TYPE_ID = 100;

    TestHandler(sequencer::EventSystem& events) : events_(events) {}

    bool onCommand(const std::string& app, zbuffer::ZBufferView& buffer) override {
        (void)app;

        // Buffer views the command payload; TYPE_ID came from the message header
        // Read command value (after messageVersion(2) + sequenceNumber(8) + msgSender(32) + sendingTimestamp(8) = 50 bytes)
        // For our simple test command, value is at offset 0
        if (buffer.readable() < sizeof(std::uint64_t)) return false;

        std::uint64_t value;
        std::memcpy(&value, buffer.read_ptr(), sizeof(value));
        buffer.skip(sizeof(value));

        // Write an event in response
        TestEventEncoder encoder;
        encoder.wrap(events_.buffer());
        encoder.setSequence(events_.nextSequence());
        encoder.setData(value * 2);
        events_.commitEventRaw(encoder);

        commands_processed++;
        return true;
    }

    int commands_processed{0};

private:
    sequencer::EventSystem& events_;
};

void test_sequencer_poll_commands() {
    std::cout << "test_sequencer_poll_commands... ";

    TestFixture fixture("poll");
    auto config = fixture.makeConfig({"app1"});

    sequencer::Sequencer seq(config);
    seq.initialize();

    TestHandler handler(seq.eventSystem());
    seq.commandSystem().registerHandler(TestHandler::TEST_CMD_TYPE_ID, handler);

    // Write commands to the queue
    auto appender = chronicle::Appender::create(config.command_queue_file("app1"));
    zbuffer::ZBuffer buf(256);

    for (int i = 1; i <= 5; i++) {
        buf.reset();
        std::uint64_t value = i * 10;
        buf.write(&value, sizeof(value));

        appender.write(buf, TestHandler::TEST_CMD_TYPE_ID);
    }

    // Poll commands
    auto processed = seq.pollCommands();
    assert(processed == 5);
    assert(handler.commands_processed == 5);
    assert(seq.stats().commands_processed == 5);
    assert(seq.eventSystem().currentSequence() == 5);  // One event per command

    std::cout << "PASSED\n";
}

void test_sequencer_multiple_apps() {
    std::cout << "test_sequencer_multiple_apps... ";

    TestFixture fixture("multi");
    auto config = fixture.makeConfig({"app1", "app2", "app3"});

    sequencer::Sequencer seq(config);
    seq.initialize();

    // Check all queues were created
    assert(fs::exists(config.command_queue_file("app1")));
    assert(fs::exists(config.command_queue_file("app2")));
    assert(fs::exists(config.command_queue_file("app3")));

    TestHandler handler(seq.eventSystem());
    seq.commandSystem().registerHandler(TestHandler::TEST_CMD_TYPE_ID, handler);

    // Write commands to different queues
    zbuffer::ZBuffer buf(256);

    for (const auto& app : {"app1", "app2", "app3"}) {
        auto appender = chronicle::Appender::create(config.command_queue_file(app));

        buf.reset();
        std::uint64_t value = 42;
        buf.write(&value, sizeof(value));

        appender.write(buf, TestHandler::TEST_CMD_TYPE_ID);
    }

    // Poll all queues
    auto processed = seq.pollCommands();
    assert(processed == 3);
    assert(handler.commands_processed == 3);

    std::cout << "PASSED\n";
}

void test_sequencer_stats() {
    std::cout << "test_sequencer_stats... ";

    TestFixture fixture("stats");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    // Register a no-op handler
    class NoOpHandler : public sequencer::CommandHandler {
    public:
        bool onCommand(const std::string&, zbuffer::ZBufferView&) override {
            return true;
        }
    };
    NoOpHandler handler;
    seq.commandSystem().registerHandler(100, handler);  // Register for type 100

    // stats() is derived from the command system on each call, so re-read it
    // rather than holding a reference to one snapshot.
    {
        const auto stats = seq.stats();
        assert(stats.commands_processed == 0);
        assert(stats.poll_cycles == 0);
        assert(stats.empty_polls == 0);
        assert(stats.errors == 0);
    }

    // Poll with no commands
    seq.pollCommands();
    {
        const auto stats = seq.stats();
        assert(stats.poll_cycles == 1);
        assert(stats.empty_polls == 1);
    }

    // Write an event via EventSystem
    auto& events = seq.eventSystem();
    TestEventEncoder encoder;
    encoder.wrap(events.buffer());
    encoder.setSequence(1);
    encoder.setData(100);
    events.commitEventRaw(encoder);
    assert(events.currentSequence() == 0);  // commitEventRaw doesn't auto-increment

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Sequencer Tests ===\n\n";

    // Clock tests
    test_system_clock();
    test_steady_clock();
    test_highres_clock();

    // Sequencer tests
    test_sequencer_initialize();
    test_sequencer_sequence_numbers();
    test_sequencer_time();
    test_event_system();
    test_sequencer_poll_commands();
    test_sequencer_multiple_apps();
    test_sequencer_stats();

    std::cout << "\nAll sequencer tests passed!\n";
    return 0;
}