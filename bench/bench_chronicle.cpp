#include <holocron/holocronQueue.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

std::filesystem::path get_bench_path() {
    auto dir = std::filesystem::current_path() / "test_output";
    std::filesystem::create_directories(dir);
    return dir / "bench_chronicle.dat";
}

int main() {
    auto path = get_bench_path();
    std::filesystem::remove(path);

    constexpr std::size_t queue_size = 256 * 1024 * 1024;  // 256 MB
    constexpr int num_messages = 1'000'000;
    constexpr std::size_t msg_size = 64;

    std::string msg(msg_size, 'X');

    double write_per_sec, write_ns_per_msg;
    double read_per_sec, read_ns_per_msg;
    int count;

    // Benchmark writes
    {
        auto appender = chronicle::Appender::create(path, queue_size);

        auto write_start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_messages; ++i) {
            appender.write(msg);
        }
        auto write_end = std::chrono::high_resolution_clock::now();

        auto write_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(write_end - write_start).count();
        write_per_sec = (double)num_messages / ((double)write_ns / 1e9);
        write_ns_per_msg = (double)write_ns / num_messages;
    } // appender closed here

    // Benchmark reads
    {
        auto tailer = chronicle::Tailer::open(path);

        auto read_start = std::chrono::high_resolution_clock::now();
        count = 0;
        while (auto m = tailer.poll()) {
            ++count;
            (void)m;
        }
        auto read_end = std::chrono::high_resolution_clock::now();

        auto read_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(read_end - read_start).count();
        read_per_sec = (double)count / ((double)read_ns / 1e9);
        read_ns_per_msg = (double)read_ns / count;
    } // tailer closed here

    std::printf("Write: %.2f M msgs/sec, %.1f ns/msg\n", write_per_sec / 1e6, write_ns_per_msg);
    std::printf("Read:  %.2f M msgs/sec, %.1f ns/msg\n", read_per_sec / 1e6, read_ns_per_msg);
    std::printf("Total: %d messages, %zu bytes each\n", count, msg_size);

    std::filesystem::remove(path);
    return 0;
}