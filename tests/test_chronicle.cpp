#include <holocron/holocronQueue.hpp>
#include <zbuffer/zbuffer.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path test_dir() {
    auto dir = std::filesystem::current_path() / "test_output";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string temp_path(const char* name) {
    return (test_dir() / (std::string("chronicle_") + name + ".dat")).string();
}

void cleanup(const std::string& path) {
    std::filesystem::remove(path);
}

} // anonymous namespace

void test_appender_create() {
    std::cout << "  appender create...\n";

    auto path = temp_path("create");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        assert(appender.capacity() > 0);
        assert(appender.position() == 0);
        assert(appender.path() == path);
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        assert(!tailer.poll());
    }

    cleanup(path);
}

void test_appender_write_string() {
    std::cout << "  appender write string...\n";

    auto path = temp_path("write_str");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        std::size_t pos0 = appender.position();

        appender.write("hello");
        std::size_t pos1 = appender.position();
        assert(pos1 > pos0);

        appender.write("world");
        std::size_t pos2 = appender.position();
        assert(pos2 > pos1);
    }

    cleanup(path);
}

void test_appender_write_bytes() {
    std::cout << "  appender write bytes...\n";

    auto path = temp_path("write_bytes");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);

        std::byte data[] = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
        appender.write(std::span<const std::byte>(data, 4));
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        auto msg = tailer.poll();
        assert(msg);
        assert(msg.length() == 4);

        auto span = msg.as_span();
        assert(span[0] == std::byte{0xDE});
        assert(span[3] == std::byte{0xEF});
    }

    cleanup(path);
}

void test_appender_write_zbuffer() {
    std::cout << "  appender write from ZBuffer...\n";

    auto path = temp_path("write_zbuf");
    cleanup(path);

    zbuffer::ZBuffer buf(256);
    (void)buf.write("prefix:", 7);
    (void)buf.write_value<std::uint32_t>(12345);
    (void)buf.write(":suffix", 7);
    std::size_t expected_len = buf.readable();

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write(buf);
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        auto msg = tailer.poll();
        assert(msg);
        assert(msg.length() == expected_len);
    }

    cleanup(path);
}

void test_tailer_open() {
    std::cout << "  tailer open...\n";

    auto path = temp_path("tailer_open");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("test message");
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        assert(tailer.position() == 0);
        assert(tailer.has_next());

        auto msg = tailer.poll();
        assert(msg);
        assert(msg.as_string() == "test message");
    }

    cleanup(path);
}

void test_tailer_from_end() {
    std::cout << "  tailer from_end...\n";

    auto path = temp_path("from_end");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("old message 1");
        appender.write("old message 2");

        auto tailer = chronicle::Tailer::open(path, true);
        assert(!tailer.has_next());
        assert(!tailer.poll());

        appender.write("new message");

        assert(tailer.has_next());
        auto msg = tailer.poll();
        assert(msg && msg.as_string() == "new message");
    }

    cleanup(path);
}

void test_tailer_poll_multiple() {
    std::cout << "  tailer poll multiple...\n";

    auto path = temp_path("poll_multi");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 8192);
        for (int i = 0; i < 100; ++i) {
            appender.write("msg " + std::to_string(i));
        }
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        for (int i = 0; i < 100; ++i) {
            auto msg = tailer.poll();
            assert(msg);
            assert(msg.as_string() == "msg " + std::to_string(i));
        }
        assert(!tailer.poll());
    }

    cleanup(path);
}

void test_tailer_poll_into_zbuffer() {
    std::cout << "  tailer poll_into ZBuffer...\n";

    auto path = temp_path("poll_into");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("buffer content");
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        zbuffer::ZBuffer buf(256);

        assert(tailer.poll_into(buf));
        assert(buf.peek_string() == "buffer content");
        assert(!tailer.poll_into(buf));
    }

    cleanup(path);
}

void test_tailer_poll_all_zbuffers() {
    std::cout << "  tailer poll_all into ZBuffers...\n";

    auto path = temp_path("poll_all_zbuf");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 8192);
        for (int i = 0; i < 5; ++i) {
            appender.write("message " + std::to_string(i));
        }
    }

    {
        std::vector<zbuffer::ZBuffer> storage;
        std::vector<zbuffer::ZBuffer*> pool;
        for (int i = 0; i < 10; ++i) {
            storage.emplace_back(256);
        }
        for (auto& b : storage) {
            pool.push_back(&b);
        }

        auto tailer = chronicle::Tailer::open(path);
        std::size_t count = tailer.poll_all(pool);

        assert(count == 5);
        assert(pool[0]->peek_string() == "message 0");
        assert(pool[4]->peek_string() == "message 4");
    }

    cleanup(path);
}

void test_tailer_poll_all_views() {
    std::cout << "  tailer poll_all MessageViews...\n";

    auto path = temp_path("poll_all_views");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 8192);
        for (int i = 0; i < 5; ++i) {
            appender.write("view " + std::to_string(i));
        }
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        std::vector<chronicle::MessageView> views;
        std::size_t count = tailer.poll_all(views);

        assert(count == 5);
        assert(views[0].as_string() == "view 0");
        assert(views[4].as_string() == "view 4");
    }

    cleanup(path);
}

void test_tailer_lag() {
    std::cout << "  tailer lag...\n";

    auto path = temp_path("lag");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("aaaa");
        appender.write("bbbb");

        auto tailer = chronicle::Tailer::open(path);
        std::size_t lag1 = tailer.lag();
        assert(lag1 > 0);

        (void)tailer.poll();
        std::size_t lag2 = tailer.lag();
        assert(lag2 < lag1);

        (void)tailer.poll();
        std::size_t lag3 = tailer.lag();
        assert(lag3 == 0);
    }

    cleanup(path);
}

void test_multi_tailer_independent() {
    std::cout << "  multi-tailer independent reads...\n";

    auto path = temp_path("multi_tailer");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("msg 1");
        appender.write("msg 2");
        appender.write("msg 3");

        auto tailer1 = chronicle::Tailer::open(path);
        auto tailer2 = chronicle::Tailer::open(path);

        assert(tailer1.poll().as_string() == "msg 1");
        assert(tailer1.poll().as_string() == "msg 2");
        assert(tailer1.poll().as_string() == "msg 3");

        assert(tailer2.poll().as_string() == "msg 1");
        assert(tailer2.poll().as_string() == "msg 2");
        assert(tailer2.poll().as_string() == "msg 3");
    }

    cleanup(path);
}

void test_live_tailing() {
    std::cout << "  live tailing (writer + reader active)...\n";

    auto path = temp_path("live");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 8192);
        auto tailer = chronicle::Tailer::open(path);

        appender.write("a");
        assert(tailer.poll().as_string() == "a");

        appender.write("b");
        appender.write("c");
        assert(tailer.poll().as_string() == "b");
        assert(tailer.poll().as_string() == "c");

        assert(!tailer.poll());

        appender.write("d");
        assert(tailer.poll().as_string() == "d");
    }

    cleanup(path);
}

void test_reopen_existing() {
    std::cout << "  reopen existing queue...\n";

    auto path = temp_path("reopen");
    cleanup(path);

    {
        auto appender = chronicle::Appender::create(path, 4096);
        appender.write("session1 msg1");
        appender.write("session1 msg2");
    }

    {
        auto appender = chronicle::Appender::create(path, 4096);
        assert(appender.position() > 0);
        appender.write("session2 msg1");
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        assert(tailer.poll().as_string() == "session1 msg1");
        assert(tailer.poll().as_string() == "session1 msg2");
        assert(tailer.poll().as_string() == "session2 msg1");
        assert(!tailer.poll());
    }

    cleanup(path);
}

void test_large_messages() {
    std::cout << "  large messages...\n";

    auto path = temp_path("large");
    cleanup(path);

    std::string large(64 * 1024, 'X');

    {
        auto appender = chronicle::Appender::create(path, 1024 * 1024);
        appender.write(large);
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        auto msg = tailer.poll();
        assert(msg);
        assert(msg.length() == large.size());
        assert(msg.as_string() == large);
    }

    cleanup(path);
}

void test_many_small_messages() {
    std::cout << "  many small messages...\n";

    auto path = temp_path("many_small");
    cleanup(path);

    const int count = 10000;

    {
        auto appender = chronicle::Appender::create(path, 1024 * 1024);
        for (int i = 0; i < count; ++i) {
            appender.write(std::to_string(i));
        }
    }

    {
        auto tailer = chronicle::Tailer::open(path);
        for (int i = 0; i < count; ++i) {
            auto msg = tailer.poll();
            assert(msg);
            assert(msg.as_string() == std::to_string(i));
        }
        assert(!tailer.poll());
    }

    cleanup(path);
}

int main() {
    std::cout << "Chronicle Tests:\n";

    test_appender_create();
    test_appender_write_string();
    test_appender_write_bytes();
    test_appender_write_zbuffer();

    test_tailer_open();
    test_tailer_from_end();
    test_tailer_poll_multiple();
    test_tailer_poll_into_zbuffer();
    test_tailer_poll_all_zbuffers();
    test_tailer_poll_all_views();
    test_tailer_lag();

    test_multi_tailer_independent();

    test_live_tailing();
    test_reopen_existing();
    test_large_messages();
    test_many_small_messages();

    std::cout << "Chronicle: ALL PASSED\n\n";
    return 0;
}