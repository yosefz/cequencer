#pragma once

/**
 * Chronicle: File-backed SWMR (single-writer multiple-reader) queue
 *
 * Writer process:
 *   #include <chronicle/chronicle.hpp>
 *
 *   auto appender = chronicle::Appender::create("/tmp/queue.dat", 64*1024*1024);
 *   appender.write("hello");
 *
 * Reader process (can be different process):
 *   #include <chronicle/chronicle.hpp>
 *
 *   auto tailer = chronicle::Tailer::open("/tmp/queue.dat");
 *   while (auto msg = tailer.poll()) {
 *       std::cout << msg.as_string() << "\n";
 *   }
 */

#include "types.hpp"
#include "mappedFile.hpp"
#include "appender.hpp"
#include "tailer.hpp"
