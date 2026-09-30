#include <holocron/holocronQueue.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

std::filesystem::path get_bench_path() {
    auto dir = std::filesystem::current_path() / "test_output";
    std::filesystem::create_directories(dir);
    return dir / "bench_concurrent.dat";
}

struct Stats {
    std::atomic<std::uint64_t> messages_written{0};
    std::atomic<std::uint64_t> messages_read{0};
    std::atomic<std::uint64_t> bytes_written{0};
    std::atomic<std::uint64_t> bytes_read{0};
    std::atomic<std::uint64_t> writer_waits{0};
    std::atomic<bool> stop{false};
};

void writer_thread(const std::filesystem::path& path, Stats& stats,
                   std::size_t msg_size, std::size_t queue_size) {
    auto appender = chronicle::Appender::create(path, queue_size);
    std::string msg(msg_size, 'X');
    std::size_t msg_total_size = msg_size + 8;  // header + payload
    std::size_t threshold = msg_total_size * 10000;  // ~10K messages buffer

    while (!stats.stop.load(std::memory_order_relaxed)) {
        // Backpressure: yield if queue is getting full
        while (appender.available() < threshold) {
            stats.writer_waits.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
            if (stats.stop.load(std::memory_order_relaxed)) return;
        }

        appender.write(msg);
        stats.messages_written.fetch_add(1, std::memory_order_relaxed);
        stats.bytes_written.fetch_add(msg_size, std::memory_order_relaxed);
    }
}

void reader_thread(const std::filesystem::path& path, Stats& stats) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto tailer = chronicle::Tailer::open(path);

    while (!stats.stop.load(std::memory_order_relaxed)) {
        if (auto msg = tailer.poll()) {
            stats.messages_read.fetch_add(1, std::memory_order_relaxed);
            stats.bytes_read.fetch_add(msg.length(), std::memory_order_relaxed);
        } else {
            std::this_thread::yield();
        }
    }

    // Drain remaining
    while (auto msg = tailer.poll()) {
        stats.messages_read.fetch_add(1, std::memory_order_relaxed);
        stats.bytes_read.fetch_add(msg.length(), std::memory_order_relaxed);
    }
}

int main(int argc, char* argv[]) {
    int duration_secs = 60;
    std::size_t msg_size = 64;
    std::size_t queue_mb = 1024;  // 1GB default

    if (argc > 1) duration_secs = std::atoi(argv[1]);
    if (argc > 2) msg_size = std::atoi(argv[2]);
    if (argc > 3) queue_mb = std::atoi(argv[3]);

    std::size_t queue_size = queue_mb * 1024 * 1024;
    std::size_t msg_total = msg_size + 8;  // header overhead
    std::size_t max_messages = queue_size / msg_total;
    double estimated_full_secs = max_messages / 20e6;  // assume ~20M msgs/sec

    std::printf("Concurrent Benchmark\n");
    std::printf("====================\n");
    std::printf("Duration:     %d seconds\n", duration_secs);
    std::printf("Message size: %d bytes (+8 header = %d total)\n",
                static_cast<int>(msg_size), static_cast<int>(msg_total));
    std::printf("Queue size:   %d MB (~%llu messages)\n",
                static_cast<int>(queue_mb), static_cast<unsigned long long>(max_messages));
    std::printf("Est. time to fill: %.1f seconds at 20M/sec\n", estimated_full_secs);
    std::printf("\nNote: Queue is linear (append-only). Writer blocks when full.\n");
    std::printf("      For longer tests, increase queue size.\n\n");

    auto path = get_bench_path();
    std::filesystem::remove(path);

    Stats stats;

    std::thread writer(writer_thread, path, std::ref(stats), msg_size, queue_size);
    std::thread reader(reader_thread, path, std::ref(stats));

    std::printf("%6s  %12s  %12s  %10s  %10s  %10s  %10s\n",
                "Time", "Written", "Read", "Write/sec", "Read/sec", "Lag", "Waits");
    std::printf("------  ------------  ------------  ----------  ----------  ----------  ----------\n");

    auto start = std::chrono::steady_clock::now();
    std::uint64_t last_written = 0, last_read = 0, last_waits = 0;

    std::vector<double> write_rates, read_rates;

    for (int sec = 1; sec <= duration_secs; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::uint64_t written = stats.messages_written.load();
        std::uint64_t read = stats.messages_read.load();
        std::uint64_t waits = stats.writer_waits.load();

        std::uint64_t wd = written - last_written;
        std::uint64_t rd = read - last_read;
        std::uint64_t lag = written - read;
        std::uint64_t wait_delta = waits - last_waits;

        write_rates.push_back(static_cast<double>(wd));
        read_rates.push_back(static_cast<double>(rd));

        std::printf("%5ds  %12llu  %12llu  %10.2fM  %10.2fM  %10llu  %10llu\n",
                    sec,
                    static_cast<unsigned long long>(written),
                    static_cast<unsigned long long>(read),
                    wd / 1e6, rd / 1e6,
                    static_cast<unsigned long long>(lag),
                    static_cast<unsigned long long>(wait_delta));

        last_written = written;
        last_read = read;
        last_waits = waits;
    }

    stats.stop.store(true);
    writer.join();
    reader.join();

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    std::uint64_t total_written = stats.messages_written.load();
    std::uint64_t total_read = stats.messages_read.load();
    std::uint64_t total_waits = stats.writer_waits.load();
    std::uint64_t bytes_written = stats.bytes_written.load();
    std::uint64_t bytes_read = stats.bytes_read.load();

    // Stats
    double write_avg = 0, read_avg = 0;
    for (auto r : write_rates) write_avg += r;
    for (auto r : read_rates) read_avg += r;
    write_avg /= write_rates.size();
    read_avg /= read_rates.size();

    double write_std = 0, read_std = 0;
    for (auto r : write_rates) write_std += (r - write_avg) * (r - write_avg);
    for (auto r : read_rates) read_std += (r - read_avg) * (r - read_avg);
    write_std = std::sqrt(write_std / write_rates.size());
    read_std = std::sqrt(read_std / read_rates.size());

    // Degradation
    std::size_t n = std::min<std::size_t>(10, write_rates.size() / 2);
    double first_w = 0, last_w = 0, first_r = 0, last_r = 0;
    for (std::size_t i = 0; i < n; ++i) {
        first_w += write_rates[i];
        first_r += read_rates[i];
        last_w += write_rates[write_rates.size() - 1 - i];
        last_r += read_rates[read_rates.size() - 1 - i];
    }
    first_w /= n; first_r /= n; last_w /= n; last_r /= n;

    std::printf("\nSummary\n=======\n");
    std::printf("Total written: %llu msgs\n", static_cast<unsigned long long>(total_written));
    std::printf("Total read:    %llu msgs\n", static_cast<unsigned long long>(total_read));
    std::printf("Lost:          %llu msgs\n", static_cast<unsigned long long>(total_written - total_read));
    std::printf("Writer waits:  %llu\n", static_cast<unsigned long long>(total_waits));
    std::printf("\nThroughput:\n");
    std::printf("  Write: %.2fM/sec avg (stddev %.2fM)\n", write_avg/1e6, write_std/1e6);
    std::printf("  Read:  %.2fM/sec avg (stddev %.2fM)\n", read_avg/1e6, read_std/1e6);
    std::printf("\nData:\n");
    std::printf("  Written: %.2f GB (%.2f GB/sec)\n", bytes_written/1e9, bytes_written/elapsed/1e9);
    std::printf("  Read:    %.2f GB (%.2f GB/sec)\n", bytes_read/1e9, bytes_read/elapsed/1e9);
    std::printf("\nDegradation (first %ds vs last %ds):\n", static_cast<int>(n), static_cast<int>(n));
    std::printf("  Write: %.2fM -> %.2fM (%+.1f%%)\n", first_w/1e6, last_w/1e6, (last_w-first_w)/first_w*100);
    std::printf("  Read:  %.2fM -> %.2fM (%+.1f%%)\n", first_r/1e6, last_r/1e6, (last_r-first_r)/first_r*100);

    std::filesystem::remove(path);
    return 0;
}