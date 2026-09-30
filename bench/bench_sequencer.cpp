/**
 * Performance benchmarks for sequencer framework.
 */
#include <sequencer/sequencer.hpp>
#include <sequencer/eventSystem.hpp>
#include <sequencer/commandSystem.hpp>
#include <apps/commander.hpp>
#include <config/config.hpp>
#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Test Messages (encoder/decoder pattern, fixed size for predictable performance)
//
// Wire format matches IDL-generated messages (BASE_MESSAGE_FIELDS):
//   offset  0: messageVersion    (uint16,    2 bytes)
//   offset  2: sequenceNumber    (uint64,    8 bytes)
//   offset 10: msgSender         (string(32),32 bytes)
//   offset 42: sendingTimestamp  (uint64,    8 bytes)
//   offset 50: <user fields>
//
// CommandSystem::SEQUENCE_NUMBER_OFFSET = 2, so seq must be at offset 2.
//------------------------------------------------------------------------------

class BenchCommandEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 100;
    static constexpr std::size_t   SIZE    = 64;
    static constexpr std::uint16_t VERSION = 1;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        buf_    = &buffer;
        offset_ = buffer.write_pos();
        // Match IDL-generated encoder: write messageVersion, zero the rest
        auto* p = reinterpret_cast<std::byte*>(buf_->write_ptr());
        std::memcpy(p, &VERSION, sizeof(VERSION));
        std::memset(p + sizeof(VERSION), 0, SIZE - sizeof(VERSION));
    }

    void commit() noexcept {
        if (buf_) buf_->advance_write(SIZE);
    }

    std::byte* data() noexcept {
        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;
    }

    // offset 0: messageVersion — set automatically in wrap()

    void setSequence(std::uint64_t v) noexcept {
        std::memcpy(data() + 2, &v, sizeof(v));   // after messageVersion(2)
    }

    void setTimestamp(std::uint64_t v) noexcept {
        std::memcpy(data() + 42, &v, sizeof(v));  // after messageVersion(2) + sequenceNumber(8) + msgSender(32)
    }

private:
    zbuffer::ZBuffer* buf_{nullptr};
    std::size_t       offset_{0};
};

class BenchEventEncoder {
public:
    static constexpr std::uint32_t TYPE_ID = 200;
    static constexpr std::size_t   SIZE    = 64;
    static constexpr std::uint16_t VERSION = 1;

    void wrap(zbuffer::ZBuffer& buffer) noexcept {
        buf_    = &buffer;
        offset_ = buffer.write_pos();
        auto* p = reinterpret_cast<std::byte*>(buf_->write_ptr());
        std::memcpy(p, &VERSION, sizeof(VERSION));
        std::memset(p + sizeof(VERSION), 0, SIZE - sizeof(VERSION));
    }

    void commit() noexcept {
        if (buf_) buf_->advance_write(SIZE);
    }

    std::byte* data() noexcept {
        return buf_ ? reinterpret_cast<std::byte*>(buf_->data()) + offset_ : nullptr;
    }

    void setSequence(std::uint64_t v) noexcept {
        std::memcpy(data() + 2, &v, sizeof(v));   // offset 2
    }

    void setTimestamp(std::uint64_t v) noexcept {
        std::memcpy(data() + 42, &v, sizeof(v));  // offset 42
    }

private:
    zbuffer::ZBuffer* buf_{nullptr};
    std::size_t       offset_{0};
};

struct BenchEvent {
    std::uint64_t sequence;
    std::uint64_t timestamp;
    std::uint64_t data[6];

    static BenchEvent decode(const std::byte* data) {
        BenchEvent evt{};
        std::memcpy(&evt.sequence,  data + 2,  sizeof(evt.sequence));   // offset 2
        std::memcpy(&evt.timestamp, data + 42, sizeof(evt.timestamp));  // offset 42
        return evt;
    }
};
static_assert(sizeof(BenchEvent) == 64);

//------------------------------------------------------------------------------
// Benchmark Utilities
//------------------------------------------------------------------------------

class BenchFixture {
public:
    BenchFixture(const std::string& name)
        : base_path_(fs::temp_directory_path() / ("bench_seq_" + name)) {
        fs::remove_all(base_path_);
        fs::create_directories(base_path_);
    }

    ~BenchFixture() {
        fs::remove_all(base_path_);
    }

    fs::path base_path() const { return base_path_; }

    config::SequencerConfig makeConfig(const std::vector<std::string>& apps = {"bench_app"}) {
        config::SequencerConfig config;
        config.name = "bench_sequencer";
        config.base_path = base_path_.string();
        config.max_command_size = 4096;
        config.max_event_size = 4096;
        config.command_queue_size = 256 * 1024 * 1024;  // 256MB
        config.event_stream_size = 256 * 1024 * 1024;   // 256MB
        config.poll_sleep_us = 0;
        config.clock_source = config::ClockSource::STEADY;
        config.apps = apps;
        return config;
    }

private:
    fs::path base_path_;
};

struct BenchResult {
    std::string name;
    int64_t operations;
    double total_time_us;
    double ops_per_sec;
    double latency_ns;
    double p50_ns;
    double p99_ns;
    double p999_ns;

    void print() const {
        std::cout << std::left << std::setw(35) << name
                  << std::right << std::setw(12) << std::fixed << std::setprecision(0)
                  << ops_per_sec << " ops/sec"
                  << std::setw(10) << std::setprecision(0) << latency_ns << " ns avg"
                  << std::setw(10) << std::setprecision(0) << p50_ns << " p50"
                  << std::setw(10) << std::setprecision(0) << p99_ns << " p99"
                  << std::setw(10) << std::setprecision(0) << p999_ns << " p999"
                  << "\n";
    }
};

BenchResult computeStats(const std::string& name, std::vector<int64_t>& latencies_ns) {
    BenchResult result;
    result.name = name;
    result.operations = latencies_ns.size();

    if (latencies_ns.empty()) {
        return result;
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());

    int64_t total_ns = std::accumulate(latencies_ns.begin(), latencies_ns.end(), int64_t{0});
    result.total_time_us = total_ns / 1000.0;
    result.latency_ns = static_cast<double>(total_ns) / latencies_ns.size();
    result.ops_per_sec = latencies_ns.size() / (result.total_time_us / 1'000'000.0);

    result.p50_ns  = latencies_ns[latencies_ns.size() * 50 / 100];
    result.p99_ns  = latencies_ns[latencies_ns.size() * 99 / 100];
    result.p999_ns = latencies_ns[std::min(latencies_ns.size() - 1, latencies_ns.size() * 999 / 1000)];

    return result;
}

//------------------------------------------------------------------------------
// Benchmarks
//------------------------------------------------------------------------------

void bench_event_write_throughput() {
    std::cout << "\n=== Event Write Throughput ===\n";

    BenchFixture fixture("write");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    auto& events = seq.eventSystem();

    const int WARMUP     = 10000;
    const int ITERATIONS = 1000000;

    for (int i = 0; i < WARMUP; i++) {
        BenchEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setSequence(i);
        events.commitEventRaw(encoder);
    }

    std::vector<int64_t> latencies;
    latencies.reserve(ITERATIONS);

    auto clock = sequencer::createClock(config::ClockSource::STEADY);

    for (int i = 0; i < ITERATIONS; i++) {
        auto start = clock->now();

        BenchEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setSequence(i);
        events.commitEventRaw(encoder);

        auto end = clock->now();
        latencies.push_back(end - start);
    }

    auto result = computeStats("commitEvent (64 bytes)", latencies);
    result.print();
}

void bench_command_dispatch_throughput() {
    std::cout << "\n=== Command Dispatch Throughput ===\n";

    BenchFixture fixture("dispatch");
    auto config = fixture.makeConfig({"app1"});

    sequencer::Sequencer seq(config);
    seq.initialize();

    class FastHandler : public sequencer::CommandHandler {
    public:
        FastHandler(sequencer::EventSystem& e) : events_(e) {}

        bool onCommand(const std::string& app, zbuffer::ZBufferView& buffer) override {
            (void)app;
            (void)buffer;
            BenchEventEncoder encoder;
            encoder.wrap(events_.buffer());
            encoder.setSequence(events_.nextSequence());
            events_.commitEventRaw(encoder);
            return true;
        }
    private:
        sequencer::EventSystem& events_;
    };

    FastHandler handler(seq.eventSystem());
    seq.commandSystem().registerHandler(BenchCommandEncoder::TYPE_ID, handler);

    const int WARMUP     = 10000;
    const int BATCH_SIZE = 10000;
    const int BATCHES    = 100;

    // Pre-create command buffer — sequence=0 is fine here since
    // bench_command_dispatch_throughput doesn't check verification correctness.
    zbuffer::ZBuffer cmd_buf(128);
    cmd_buf.reset();
    BenchCommandEncoder encoder;
    encoder.wrap(cmd_buf);
    encoder.setSequence(0);
    encoder.commit();

    {
        auto appender = chronicle::Appender::create(config.command_queue_file("app1"));
        for (int i = 0; i < WARMUP; i++) {
            appender.write(cmd_buf);
        }
    }
    seq.pollCommands();

    std::vector<int64_t> latencies;
    latencies.reserve(BATCHES);

    auto clock = sequencer::createClock(config::ClockSource::STEADY);

    for (int batch = 0; batch < BATCHES; batch++) {
        auto appender = chronicle::Appender::create(config.command_queue_file("app1"));
        for (int i = 0; i < BATCH_SIZE; i++) {
            appender.write(cmd_buf);
        }

        auto start     = clock->now();
        auto processed = seq.pollCommands();
        auto end       = clock->now();

        assert(processed == BATCH_SIZE);
        latencies.push_back((end - start) / BATCH_SIZE);
    }

    auto result = computeStats("pollCommands -> commitEvent", latencies);
    result.ops_per_sec = (BATCHES * BATCH_SIZE) / (result.total_time_us / 1'000'000.0);
    result.print();

    std::cout << "  Total commands: " << seq.stats().commands_processed << "\n";
    std::cout << "  Total events: " << seq.eventSystem().currentSequence() << "\n";
}

void bench_end_to_end_latency() {
    std::cout << "\n=== End-to-End Latency (Command -> Event) ===\n";

    BenchFixture fixture("e2e");
    auto config = fixture.makeConfig({"bench_app"});

    sequencer::Sequencer seq(config);
    seq.initialize();

    class TimingHandler : public sequencer::CommandHandler {
    public:
        TimingHandler(sequencer::EventSystem& e) : events_(e) {}

        bool onCommand(const std::string& app, zbuffer::ZBufferView& buffer) override {
            (void)app;
            // Buffer is positioned after TYPE_ID.
            // sendingTimestamp is at offset 42 in the IDL wire format:
            //   messageVersion(2) + sequenceNumber(8) + msgSender(32) = 42
            std::uint64_t cmd_timestamp;
            std::memcpy(&cmd_timestamp, buffer.read_ptr() + 42, sizeof(cmd_timestamp));

            BenchEventEncoder encoder;
            encoder.wrap(events_.buffer());
            encoder.setSequence(events_.nextSequence());
            encoder.setTimestamp(cmd_timestamp);
            events_.commitEventRaw(encoder);
            return true;
        }
    private:
        sequencer::EventSystem& events_;
    };

    TimingHandler handler(seq.eventSystem());
    seq.commandSystem().registerHandler(BenchCommandEncoder::TYPE_ID, handler);

    const int ITERATIONS = 100000;

    auto clock    = sequencer::createClock(config::ClockSource::STEADY);
    auto appender = chronicle::Appender::create(config.command_queue_file("bench_app"));
    auto tailer   = chronicle::Tailer::open(config.event_stream_file());

    while (tailer.poll()) {}  // Skip existing events

    std::vector<int64_t> latencies;
    latencies.reserve(ITERATIONS);

    zbuffer::ZBuffer buf(128);

    for (int i = 0; i < ITERATIONS; i++) {
        buf.reset();

        BenchCommandEncoder encoder;
        encoder.wrap(buf);
        encoder.setSequence(i);
        encoder.setTimestamp(clock->now());
        encoder.commit();

        appender.write(buf);
        seq.pollCommands();

        if (auto view = tailer.poll()) {
            auto evt = BenchEvent::decode(view.data() + sizeof(std::uint32_t));
            auto end = clock->now();
            latencies.push_back(end - evt.timestamp);
        }
    }

    auto result = computeStats("Command -> Sequencer -> Event", latencies);
    result.print();
}

void bench_clock_overhead() {
    std::cout << "\n=== Clock Overhead ===\n";

    const int ITERATIONS = 1000000;

    for (auto source : {config::ClockSource::SYSTEM,
                        config::ClockSource::STEADY,
                        config::ClockSource::HIGH_RES}) {

        auto clock = sequencer::createClock(source);

        std::vector<int64_t> latencies;
        latencies.reserve(ITERATIONS);

        for (int i = 0; i < ITERATIONS; i++) {
            auto start = clock->now();
            auto t     = clock->now();
            auto end   = clock->now();
            (void)t;
            latencies.push_back(end - start);
        }

        std::string name = "Clock::now() - ";
        switch (source) {
            case config::ClockSource::SYSTEM:   name += "SYSTEM";   break;
            case config::ClockSource::STEADY:   name += "STEADY";   break;
            case config::ClockSource::HIGH_RES: name += "HIGH_RES"; break;
        }

        auto result = computeStats(name, latencies);
        result.print();
    }
}

void bench_commander_send() {
    std::cout << "\n=== Commander Send Throughput ===\n";

    BenchFixture fixture("commander");
    auto config = fixture.makeConfig({"bench_app"});

    sequencer::Sequencer seq(config);
    seq.initialize();

    apps::Commander commander(config.command_queue_file("bench_app"), 5'000'000'000, 1, 0);

    const int WARMUP     = 10000;
    const int ITERATIONS = 1000000;

    auto clock = sequencer::createClock(config::ClockSource::STEADY);

    zbuffer::ZBuffer cmd_buf(128);
    cmd_buf.reset();
    BenchCommandEncoder encoder;
    encoder.wrap(cmd_buf);
    encoder.setSequence(0);
    encoder.commit();

    for (int i = 0; i < WARMUP; i++) {
        commander.sendRaw(cmd_buf, BenchCommandEncoder::TYPE_ID, clock->now());
        commander.onAck();
    }

    std::vector<int64_t> latencies;
    latencies.reserve(ITERATIONS);

    for (int i = 0; i < ITERATIONS; i++) {
        auto start = clock->now();
        commander.sendRaw(cmd_buf, BenchCommandEncoder::TYPE_ID, start);
        auto end = clock->now();

        commander.onAck();
        latencies.push_back(end - start);
    }

    auto result = computeStats("Commander::sendRaw (64 bytes)", latencies);
    result.print();
}

void bench_sequence_number() {
    std::cout << "\n=== Sequence Number Generation ===\n";

    BenchFixture fixture("seqnum");
    auto config = fixture.makeConfig();

    sequencer::Sequencer seq(config);
    seq.initialize();

    auto& events = seq.eventSystem();

    const int ITERATIONS = 10000000;

    std::vector<int64_t> latencies;
    latencies.reserve(ITERATIONS);

    auto clock = sequencer::createClock(config::ClockSource::STEADY);

    for (int i = 0; i < ITERATIONS; i++) {
        auto start = clock->now();
        auto n     = events.nextSequence();
        auto end   = clock->now();
        (void)n;

        latencies.push_back(end - start);
    }

    auto result = computeStats("nextSequence()", latencies);
    result.print();
}

int main() {
    std::cout << "=============================================================\n";
    std::cout << "           Sequencer Framework Performance Benchmarks\n";
    std::cout << "=============================================================\n";

    bench_clock_overhead();
    bench_sequence_number();
    bench_event_write_throughput();
    bench_commander_send();
    bench_command_dispatch_throughput();
    bench_end_to_end_latency();

    std::cout << "\n=============================================================\n";
    std::cout << "                      Benchmarks Complete\n";
    std::cout << "=============================================================\n";

    return 0;
}