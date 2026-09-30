# Sequencer Library - CLion Integration Guide

## Library Structure

```
sequencer/
├── CMakeLists.txt                      # Root CMake configuration
├── README.md
├── CLION.md                            # This file
│
├── idl/                                # IDL code generation
│   ├── codegen.py                      # Code generator script
│   ├── schema.idl                      # Example schema (your project will have its own)
│   └── README.md
│
├── libs/
│   ├── zbuffer/                        # Zero-copy buffer
│   │   ├── CMakeLists.txt
│   │   ├── include/zbuffer/
│   │   │   └── zbuffer.hpp
│   │   └── src/
│   │       └── zbuffer.cpp
│   │
│   ├── chronicle/                      # Memory-mapped queue (like Chronicle Queue)
│   │   ├── CMakeLists.txt
│   │   ├── include/chronicle/
│   │   │   ├── holocronQueue.hpp       # Umbrella header
│   │   │   ├── appender.hpp
│   │   │   ├── tailer.hpp
│   │   │   ├── mappedFile.hpp
│   │   │   └── types.hpp
│   │   └── src/
│   │       ├── appender.cpp
│   │       ├── tailer.cpp
│   │       └── mapped_file.cpp
│   │
│   ├── config/                         # Configuration loading
│   │   ├── CMakeLists.txt
│   │   ├── include/config/
│   │   │   ├── config.hpp              # AppConfig, SequencerConfig, StreamAppConfig
│   │   │   └── configLoader.hpp        # ConfigLoader
│   │   └── src/
│   │       └── loader.cpp
│   │
│   ├── sequencer/                      # Core sequencer framework
│   │   ├── CMakeLists.txt
│   │   ├── include/sequencer/
│   │   │   ├── all.hpp                 # Umbrella header
│   │   │   ├── clock.hpp               # Clock, SystemClock, SteadyClock, HighResClock
│   │   │   ├── pollable.hpp            # Pollable interface
│   │   │   ├── app.hpp                 # App base class
│   │   │   ├── stream_app.hpp          # StreamApp (time-aware apps)
│   │   │   ├── sequencer.hpp           # Core Sequencer logic
│   │   │   ├── sequencer_app.hpp       # SequencerApp wrapper
│   │   │   ├── event_stream_app.hpp    # EventStreamApp (tails events)
│   │   │   ├── event_stream_writer.hpp # Helper for writing events
│   │   │   └── command_queue_writer.hpp# Helper for writing commands
│   │   └── src/
│   │       ├── app.cpp
│   │       ├── stream_app.cpp
│   │       ├── sequencer.cpp
│   │       ├── sequencer_app.cpp
│   │       ├── event_stream_app.cpp
│   │       └── command_queue_writer.cpp
│   │
│   └── messages/                       # Generated from IDL (example)
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── common/
│       │   │   └── List.hpp            # Shared List<T,N> template
│       │   └── schema/
│       │       ├── all.hpp
│       │       ├── enums/
│       │       │   ├── all.hpp
│       │       │   ├── Side.hpp
│       │       │   ├── OrderType.hpp
│       │       │   ├── OrderStatus.hpp
│       │       │   └── TimeInForce.hpp
│       │       ├── objects/
│       │       │   ├── all.hpp
│       │       │   └── PriceLevel.hpp
│       │       ├── commands/
│       │       │   ├── all.hpp
│       │       │   ├── NewOrderSingleCommand.hpp
│       │       │   ├── CancelOrderCommand.hpp
│       │       │   └── ModifyOrderCommand.hpp
│       │       ├── events/
│       │       │   ├── all.hpp
│       │       │   ├── StartOfSessionEvent.hpp
│       │       │   ├── TimeEvent.hpp
│       │       │   ├── ExecutionReportEvent.hpp
│       │       │   ├── MarketDataEvent.hpp
│       │       │   └── TradeEvent.hpp
│       │       └── handlers/
│       │           ├── all.hpp
│       │           ├── CommandHandler.hpp
│       │           ├── Dispatcher.hpp
│       │           └── EventWriter.hpp
│       └── src/
│           └── schema/commands/
│               ├── NewOrderSingleCommand.cpp
│               ├── CancelOrderCommand.cpp
│               └── ModifyOrderCommand.cpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_zbuffer.cpp
│   ├── test_chronicle.cpp
│   ├── test_messages.cpp
│   └── test_all.cpp
│
└── bench/
    ├── CMakeLists.txt
    ├── bench_chronicle.cpp
    └── bench_concurrent.cpp
```

## CMake Targets

| Target | Alias | Description |
|--------|-------|-------------|
| `zbuffer` | `seq::zbuffer` | Zero-copy buffer |
| `chronicle` | `seq::chronicle` | Memory-mapped queue |
| `config` | `seq::config` | Configuration loading |
| `sequencer_core` | `seq::sequencer` | Sequencer framework |
| `messages` | `seq::messages` | Generated messages (example) |

---

## Integrating into Your Project

### Option 1: Git Submodule (Recommended)

**1. Add as submodule:**
```bash
cd your-project
git submodule add https://github.com/yourorg/sequencer.git extern/sequencer
git submodule update --init --recursive
```

**2. Your project structure:**
```
your-trading-app/
├── CMakeLists.txt
├── extern/
│   └── sequencer/              # Git submodule
├── idl/
│   └── trading.idl             # YOUR schema
├── libs/
│   └── messages/               # Generated from YOUR schema
├── apps/
│   ├── sequencer/
│   │   └── main.cpp
│   └── trading_engine/
│       └── main.cpp
└── src/
    └── ...
```

**3. Your root CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.20)
project(trading_system VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add sequencer library (but skip its example messages)
set(SEQUENCER_BUILD_MESSAGES OFF CACHE BOOL "" FORCE)
set(SEQUENCER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SEQUENCER_BUILD_BENCH OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/sequencer)

# Your generated messages
add_subdirectory(libs/messages)

# Your applications
add_subdirectory(apps/sequencer)
add_subdirectory(apps/trading_engine)
```

**4. Your app CMakeLists.txt:**
```cmake
# apps/sequencer/CMakeLists.txt
add_executable(my_sequencer main.cpp)
target_link_libraries(my_sequencer PRIVATE
    seq::sequencer
    my::messages      # Your generated messages
)
```

---

### Option 2: FetchContent

**Your CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.20)
project(trading_system)

include(FetchContent)

FetchContent_Declare(
    sequencer
    GIT_REPOSITORY https://github.com/yourorg/sequencer.git
    GIT_TAG        main
)

set(SEQUENCER_BUILD_MESSAGES OFF)
set(SEQUENCER_BUILD_TESTS OFF)
set(SEQUENCER_BUILD_BENCH OFF)
FetchContent_MakeAvailable(sequencer)

# Your code
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE seq::sequencer)
```

---

### Option 3: Installed Library

**1. Build and install sequencer:**
```bash
cd sequencer
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo make install
```

**2. Use find_package:**
```cmake
find_package(sequencer REQUIRED)
target_link_libraries(my_app PRIVATE seq::sequencer)
```

---

## CLion Setup

### Opening as Submodule Project

1. **Open your project** in CLion (the parent project, not sequencer)

2. **CMake will auto-detect** the submodule via `add_subdirectory()`

3. **Build configurations** - CLion will show all targets including:
    - `seq::zbuffer`
    - `seq::chronicle`
    - `seq::config`
    - `seq::sequencer`
    - Your app targets

### Code Generation Workflow

1. **Edit your IDL file:** `idl/trading.idl`

2. **Run codegen** (add as CMake custom command or run manually):
   ```bash
   cd idl
   python3 codegen.py -o ../libs/messages/
   ```

3. **Reload CMake** in CLion: `Tools → CMake → Reload CMake Project`

### Recommended CLion Settings

**File → Settings → Build, Execution, Deployment → CMake:**
```
CMake options: -DCMAKE_BUILD_TYPE=Debug
Build directory: cmake-build-debug
```

For Release:
```
CMake options: -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
Build directory: cmake-build-release
```

---

## Example: Your Sequencer App

```cpp
// apps/sequencer/main.cpp
#include <sequencer/all.hpp>
#include <config/configLoader.hpp>
#include <trading/all.hpp>  // Your generated messages

namespace events = messages::trading::events;
namespace commands = messages::trading::commands;
namespace handlers = messages::trading::handlers;

class MyEventWriter : public apps::EventStreamWriter<handlers::EventWriter> {
public:
    using EventStreamWriter::EventStreamWriter;
    
    void write(const events::ExecutionReportEvent& e) override { writeEvent(e); }
    void write(const events::StartOfSessionEvent& e) override { writeEvent(e); }
    void write(const events::TimeEvent& e) override { writeEvent(e); }
    // ... other events
};

class MyHandler : public handlers::CommandHandler {
public:
    MyHandler(sequencer::Sequencer& seq, MyEventWriter& writer)
        : seq_(seq), writer_(writer) {}
    
    void onNewOrderSingle(const commands::NewOrderSingleCommand& cmd,
                          handlers::EventWriter& w) override {
        // Handle order...
        events::ExecutionReportEvent exec;
        exec.sequenceNumber = seq_.nextSequence();
        exec.sendingTimestamp = seq_.now();
        // ... fill in fields
        w.write(exec);
    }
    
private:
    sequencer::Sequencer& seq_;
    MyEventWriter& writer_;
};

class MyHandler : public sequencer::CommandHandler {
public:
    MyHandler(OrderHandler& h, MyEventWriter& w) : handler_(h), writer_(w) {}
    
    bool onCommand(const std::string& app, zbuffer::ZBufferView& buffer) override {
        // Buffer is positioned after TYPE_ID
        // Dispatch based on previously read TYPE_ID (from CommandSystem)
        // Or use a generated dispatcher here
        return handler_.handleNewOrder(buffer);
    }
    
private:
    OrderHandler& handler_;
    MyEventWriter& writer_;
};

int main(int argc, char* argv[]) {
    auto config = config::ConfigLoader::loadSequencerConfig(argv[1]);
    apps::SequencerApp app(config);
    
    // Set up system event publishers
    app.setPublishStartOfSession([](sequencer::EventSystem& events, const std::string& sessionId) {
        events::StartOfSessionEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setMsgSender("SEQUENCER");
        encoder.setSessionId(sessionId);
        events.commitEvent(encoder);
    });
    
    app.setPublishTime([](sequencer::EventSystem& events) {
        events::TimeEventEncoder encoder;
        encoder.wrap(events.buffer());
        encoder.setMsgSender("SEQUENCER");
        events.commitEvent(encoder);
    });
    
    MyEventWriter writer(app.sequencer());
    OrderHandler orderHandler(app.sequencer(), writer);
    MyHandler handler(orderHandler, writer);
    
    // Register handlers by TYPE_ID
    app.commandSystem().registerHandler(NewOrderSingleCommand::TYPE_ID, handler);
    app.commandSystem().registerHandler(CancelOrderCommand::TYPE_ID, handler);
    
    app.setTimeEventInterval(1'000'000'000);  // 1 second
    app.run();
    
    return 0;
}
```

## Example: Your EventStreamApp

```cpp
// apps/trading_engine/main.cpp
#include <sequencer/all.hpp>
#include <trading/all.hpp>

namespace events = messages::trading::events;

class TradingEngine : public apps::EventStreamApp {
public:
    TradingEngine(const config::StreamAppConfig& cfg)
        : EventStreamApp(cfg) {
        
        setSystemEventHandler([](uint32_t type_id, const std::byte* data, size_t len)
            -> apps::SystemEventResult {
            if (type_id == events::TimeEventDecoder::TYPE_ID) {
                events::TimeEventDecoder d;
                d.wrap(zbuffer::ZBufferView(data, len));
                return {.kind = apps::SystemEventKind::Handled,
                        .timestamp = d.getTimestamp()};
            }
            if (type_id == events::StartOfSessionEventDecoder::TYPE_ID) {
                events::StartOfSessionEventDecoder d;
                d.wrap(zbuffer::ZBufferView(data, len));
                // string_view into the event buffer, valid for this call only
                return {.kind = apps::SystemEventKind::SessionStart,
                        .session_id = d.getSessionIdAsString()};
            }
            return {};
        });
    }

protected:
    void onEvent(uint64_t nanos, uint32_t type_id,
                 const std::byte* data, size_t len) override {
        switch (type_id) {
            case events::ExecutionReportEvent::TYPE_ID: {
                auto e = events::ExecutionReportEvent::decode(data);
                handleExecution(nanos, e);
                break;
            }
        }
    }
    
private:
    void handleExecution(uint64_t nanos, const events::ExecutionReportEvent& e) {
        // Process at time 'nanos'
    }
};

int main(int argc, char* argv[]) {
    auto config = config::ConfigLoader::loadStreamAppConfig(argv[1]);
    TradingEngine engine(config);
    engine.run();
    return 0;
}
```