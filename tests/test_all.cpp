#include <zbuffer/zbuffer.hpp>
#include <holocron/holocronQueue.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

std::filesystem::path test_dir() {
    auto dir = std::filesystem::current_path() / "test_output";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string temp_path(const char* name) {
    return (test_dir() / (std::string("test_") + name + ".dat")).string();
}

void cleanup(const std::string& path) {
    std::filesystem::remove(path);
}

void test_zbuffer() {
    std::cout << "Testing ZBuffer...\n";

    zbuffer::ZBuffer buf(1024);

    assert(buf.write("hello", 5));
    assert(buf.write_value<std::uint32_t>(42));
    assert(buf.readable() == 9);

    char str[6] = {};
    assert(buf.read(str, 5));
    assert(std::string(str) == "hello");

    std::uint32_t val;
    assert(buf.read_value(val));
    assert(val == 42);
    assert(buf.empty());

    buf.reset();
    assert(buf.writable() == 1024);

    std::cout << "  ZBuffer: PASSED\n";
}

void test_chronicle_basic() {
    std::cout << "Testing Chronicle basic...\n";

    auto path = temp_path("basic");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("message 1");
        appender.write("message 2");
        appender.write("message 3");
    }

    {
        auto tailer = chronicle::Tailer::open(path);

        auto msg1 = tailer.poll();
        assert(msg1 && msg1.as_string() == "message 1");

        auto msg2 = tailer.poll();
        assert(msg2 && msg2.as_string() == "message 2");

        auto msg3 = tailer.poll();
        assert(msg3 && msg3.as_string() == "message 3");

        assert(!tailer.poll());
    }

    cleanup(path);
    std::cout << "  Basic: PASSED\n";
}

void test_chronicle_live_tailing() {
    std::cout << "Testing Chronicle live tailing...\n";

    auto path = temp_path("live");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("first");

        auto tailer = chronicle::Tailer::open(path);

        auto msg1 = tailer.poll();
        assert(msg1 && msg1.as_string() == "first");
        assert(!tailer.poll());

        appender.write("second");
        appender.write("third");

        auto msg2 = tailer.poll();
        assert(msg2 && msg2.as_string() == "second");

        auto msg3 = tailer.poll();
        assert(msg3 && msg3.as_string() == "third");

        assert(!tailer.poll());
    }

    cleanup(path);
    std::cout << "  Live tailing: PASSED\n";
}

void test_chronicle_multi_tailer() {
    std::cout << "Testing Chronicle multi-tailer...\n";

    auto path = temp_path("multi");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("msg A");
        appender.write("msg B");

        auto tailer1 = chronicle::Tailer::open(path);
        auto tailer2 = chronicle::Tailer::open(path);

        assert(tailer1.poll().as_string() == "msg A");
        assert(tailer2.poll().as_string() == "msg A");

        assert(tailer1.poll().as_string() == "msg B");
        assert(tailer2.poll().as_string() == "msg B");
    }

    cleanup(path);
    std::cout << "  Multi-tailer: PASSED\n";
}

void test_chronicle_from_end() {
    std::cout << "Testing Chronicle from_end...\n";

    auto path = temp_path("fromend");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("old message");

        auto tailer = chronicle::Tailer::open(path, true);
        assert(!tailer.poll());

        appender.write("new message");
        auto msg = tailer.poll();
        assert(msg && msg.as_string() == "new message");
    }

    cleanup(path);
    std::cout << "  From end: PASSED\n";
}

void test_chronicle_poll_into_zbuffer() {
    std::cout << "Testing Chronicle poll into ZBuffer...\n";

    auto path = temp_path("pollinto");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("test data here");
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        zbuffer::ZBuffer buf(256);

        assert(tailer.poll_into(buf));
        assert(buf.peek_string() == "test data here");
    }

    cleanup(path);
    std::cout << "  Poll into ZBuffer: PASSED\n";
}

void test_chronicle_poll_all() {
    std::cout << "Testing Chronicle poll_all...\n";

    auto path = temp_path("pollall");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 8192);
        for (int i = 0; i < 5; ++i) {
            appender.write("msg " + std::to_string(i));
        }
    }

    {
        std::vector<zbuffer::ZBuffer> pool_storage;
        std::vector<zbuffer::ZBuffer*> pool;
        for (int i = 0; i < 10; ++i) {
            pool_storage.emplace_back(256);
        }
        for (auto& b : pool_storage) {
            pool.push_back(&b);
        }

        auto tailer = chronicle::Tailer::open(path);
        std::size_t count = tailer.poll_all(pool);
        assert(count == 5);
        assert(pool[0]->peek_string() == "msg 0");
        assert(pool[4]->peek_string() == "msg 4");
    }

    cleanup(path);
    std::cout << "  Poll all: PASSED\n";
}

void test_chronicle_zbuffer_write() {
    std::cout << "Testing Chronicle write from ZBuffer...\n";

    auto path = temp_path("zbufwrite");
    cleanup(path);

    zbuffer::ZBuffer buf(256);
    (void)buf.write("header:", 7);
    (void)buf.write_value<std::uint32_t>(12345);
    (void)buf.write(":tail", 5);
    std::size_t expected = buf.readable();

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write(buf);
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        auto msg = tailer.poll();
        assert(msg.length() == expected);
    }

    cleanup(path);
    std::cout << "  Write from ZBuffer: PASSED\n";
}

int main() {
    std::cout << "\n=== Sequencer Library Tests ===\n\n";

    test_zbuffer();
    test_chronicle_basic();
    test_chronicle_live_tailing();
    test_chronicle_multi_tailer();
    test_chronicle_from_end();
    test_chronicle_poll_into_zbuffer();
    test_chronicle_poll_all();
    test_chronicle_zbuffer_write();

    std::cout << "\n=== All tests PASSED ===\n\n";
    return 0;
}