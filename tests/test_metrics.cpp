/**
 * Tests for metrics library.
 */
#include <metrics/all.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

constexpr std::uint64_t SEC = metrics::TimeWindowCounter::ONE_SECOND_NS;

fs::path test_dir() {
    auto dir = fs::temp_directory_path() / "test_metrics";
    fs::create_directories(dir);
    return dir;
}

std::vector<std::string> read_lines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

void test_counter() {
    std::cout << "Testing Counter...\n";

    metrics::Counter c("orders.received");
    assert(c.value() == 0);

    c.increment();
    c.increment(5);
    assert(c.value() == 6);

    // Sampling a counter is non-destructive.
    assert(c.sample(0) == 6);
    assert(c.sample(0) == 6);

    assert(c.name() == "orders.received");
    assert(c.typeName() == "counter");

    std::cout << "  Counter: PASSED\n";
}

void test_gauge() {
    std::cout << "Testing Gauge...\n";

    metrics::Gauge g("commands.inflight");

    g.set(10);
    assert(g.value() == 10);

    g.add(5);
    g.subtract(3);
    assert(g.value() == 12);

    // Unlike a counter, a gauge may move backwards past zero.
    g.set(-4);
    assert(g.value() == -4);
    assert(g.sample(0) == -4);
    assert(g.value() == -4);

    std::cout << "  Gauge: PASSED\n";
}

void test_exemplar_gauge() {
    std::cout << "Testing ExemplarGauge...\n";

    metrics::ExemplarGauge peak("queue.depth.peak");

    peak.observe(10);
    peak.observe(4);
    peak.observe(27);
    peak.observe(12);
    assert(peak.value() == 27);

    // sample() publishes the peak and starts the next interval clean.
    assert(peak.sample(0) == 27);
    assert(peak.value() == 0);

    peak.observe(3);
    assert(peak.sample(0) == 3);

    std::cout << "  ExemplarGauge: PASSED\n";
}

void test_time_window_counter() {
    std::cout << "Testing TimeWindowCounter...\n";

    metrics::TimeWindowCounter tw("events.per.5s", 5);
    assert(tw.windowSeconds() == 5);

    std::uint64_t now = 100 * SEC;

    tw.increment(3, now);
    assert(tw.sample(now) == 3);

    // Same second accumulates into the same bucket.
    tw.increment(2, now);
    assert(tw.sample(now) == 5);

    // Spread across the window: everything is still in range.
    for (int i = 1; i < 5; ++i) {
        tw.increment(1, now + i * SEC);
    }
    assert(tw.sample(now + 4 * SEC) == 9);

    // Step one second past the window: the oldest bucket (5) falls out.
    assert(tw.sample(now + 5 * SEC) == 4);

    // Two more seconds drop the next two buckets.
    assert(tw.sample(now + 7 * SEC) == 2);

    std::cout << "  Rolling window: PASSED\n";
}

void test_time_window_counter_gap() {
    std::cout << "Testing TimeWindowCounter gaps...\n";

    metrics::TimeWindowCounter tw("events.per.3s", 3);
    const std::uint64_t now = 1000 * SEC;

    tw.increment(7, now);
    assert(tw.sample(now) == 7);

    // A gap longer than the whole window clears it, without walking one bucket
    // per elapsed second.
    assert(tw.sample(now + 10'000 * SEC) == 0);

    // Still usable afterwards.
    tw.increment(4, now + 10'000 * SEC);
    assert(tw.sample(now + 10'000 * SEC) == 4);

    std::cout << "  Long gap: PASSED\n";
}

void test_registry_write() {
    std::cout << "Testing Registry write...\n";

    const auto dir = test_dir() / "write";
    fs::remove_all(dir);

    metrics::Registry registry("sequencer", dir);
    auto& processed = registry.add<metrics::Counter>("commands.processed");
    auto& depth = registry.add<metrics::Gauge>("queue.depth");
    auto& peak = registry.add<metrics::ExemplarGauge>("queue.depth.peak");
    assert(registry.size() == 3);

    processed.increment(42);
    depth.set(7);
    peak.observe(19);

    registry.write(1234);

    assert(fs::exists(registry.outputPath()));
    assert(!fs::exists(dir / "sequencer.txt.tmp"));

    const auto lines = read_lines(registry.outputPath());
    assert(lines.size() == 3);
    assert(lines[0] == "app=sequencer metric=commands.processed type=counter value=42 ts=1234");
    assert(lines[1] == "app=sequencer metric=queue.depth type=gauge value=7 ts=1234");
    assert(lines[2] == "app=sequencer metric=queue.depth.peak type=exemplar value=19 ts=1234");

    // The dump sampled the exemplar, so the next one starts from zero.
    registry.write(5678);
    const auto second = read_lines(registry.outputPath());
    assert(second.size() == 3);
    assert(second[2] == "app=sequencer metric=queue.depth.peak type=exemplar value=0 ts=5678");

    fs::remove_all(dir);
    std::cout << "  Registry write: PASSED\n";
}

void test_registry_write_if_due() {
    std::cout << "Testing Registry write interval...\n";

    const auto dir = test_dir() / "interval";
    fs::remove_all(dir);

    metrics::Registry registry("engine", dir, SEC);
    auto& c = registry.add<metrics::Counter>("ticks");
    c.increment();

    // First call always writes so there is a file to read straight away.
    assert(registry.writeIfDue(0));
    assert(fs::exists(registry.outputPath()));

    assert(!registry.writeIfDue(SEC / 2));
    assert(!registry.writeIfDue(SEC - 1));
    assert(registry.writeIfDue(SEC));
    assert(!registry.writeIfDue(SEC + 1));
    assert(registry.writeIfDue(2 * SEC));

    fs::remove_all(dir);
    std::cout << "  Write interval: PASSED\n";
}

void test_registry_owns_metrics() {
    std::cout << "Testing Registry ownership...\n";

    const auto dir = test_dir() / "ownership";
    fs::remove_all(dir);

    metrics::Registry registry("app", dir);
    auto& first = registry.add<metrics::Counter>("first");
    first.increment(1);

    // Adding more metrics must not invalidate references handed out earlier.
    for (int i = 0; i < 64; ++i) {
        registry.add<metrics::Counter>("filler." + std::to_string(i));
    }
    first.increment(1);
    assert(first.value() == 2);
    assert(registry.size() == 65);

    fs::remove_all(dir);
    std::cout << "  Ownership: PASSED\n";
}

int main() {
    std::cout << "\n=== Metrics Tests ===\n\n";

    test_counter();
    test_gauge();
    test_exemplar_gauge();
    test_time_window_counter();
    test_time_window_counter_gap();
    test_registry_write();
    test_registry_write_if_due();
    test_registry_owns_metrics();

    fs::remove_all(test_dir());

    std::cout << "\n=== All tests PASSED ===\n\n";
    return 0;
}
