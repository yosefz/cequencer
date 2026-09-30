# Sequencer (where a lot of magic happens)

A C++26 framework for building a Sequencer based trading system (or whatever you want to use a Sequencer for!)

In a Sequencer ring, apps never talk to each other directly. Each one writes commands into its own memory-mapped queue -
think Chronicle but not actually Chronicle. One Sequencer thread drains every queue, processes the command, 
and appends the result to a single event stream that every app tails. 
That stream is the system's only source of truth, so any app can be rebuilt by replaying it.

```
  app A ──┐
  app B ──┼──► commands/<app>.dat  ──►  Sequencer  ──►  events/<name>.dat
  app C ──┘     (one queue each,          (single          (total order,
                 SWMR mmap)                writer)          SWMR mmap)
                                                                 │
  app A ◄────────────────── tail ────────────────────────────────┤
  app B ◄────────────────── tail ────────────────────────────────┤
  app C ◄────────────────── tail ────────────────────────────────┘
```

The focus here is low-latency as 1a and throughput as 1b: fixed-size wire layouts, no allocation on
the hot path, optional busy-spin polling, and `-O3 -march=native` in release builds.

## Libraries

| Target | Purpose |
|--------|---------|
| `seq::zbuffer` | `ZBuffer` (read/write byte buffer + pool) and `ZBufferView` (read-only, trivially copyable) |
| `seq::chronicle` | Memory-mapped single-writer/multi-reader queue — `Appender`, `Tailer`, headers under `holocron/` |
| `seq::config` | `SequencerConfig` / `StreamAppConfig` and the key=value loader |
| `seq::metrics` | Counters, gauges, exemplar gauges, rolling-window counters, and a text-file registry |
| `seq::sequencer` | `Clock`, `EventSystem`, `CommandSystem`, `Sequencer` |
| `seq::apps` | `App`, `StreamApp`, `EventStreamApp`, `SequencerApp`, `Commander`, `Pollable` |
| `seq::messages` | Generated from `idl/schema.idl` — example only, your project brings its own |

`seq::metrics` deliberately links nothing: time is passed in by the caller rather than read from a
clock, so it stays a leaf and can be used to instrument any of the others.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build/tests
```

The example message library is generated, and should not be committed. Regenerate it after editing the schema:

```bash
cd idl
python3 codegen.py --idl-dir . --output ../libs/messages/
```

As a submodule, turn off the parts you don't want:

```cmake
set(SEQUENCER_BUILD_MESSAGES OFF CACHE BOOL "" FORCE)
set(SEQUENCER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(SEQUENCER_BUILD_BENCH    OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/sequencer)
```

## Wire format

Queue files open with a cache-line-aligned `FileHeader`; `write_pos` is the only synchronization
point between writer and readers. Publishing is a release store to it, consuming is an acquire load of the 
new events.

```
FileHeader (64B) | [length:4][type_id:4][payload][pad to 8] | ...
```

`length == 0` marks a write in progress. `type_id` lives in the header rather than the payload, so
readers get it in the same cache line as the length and writers don't prepend it by hand. Slots are
padded to 8 bytes so no header or field is ever misaligned.

`queue_version` is 2. Version 1 files (type id inside the payload, unpadded slots) are rejected at
open rather than misread.

## Schema

`idl/schema.idl` defines enums, embedded objects, commands and events; `codegen.py` turns them into
fixed-offset encoder/decoder pairs plus handler interfaces and a dispatcher. Every message needs a
`@typeId(N)`, which is what the command/event dispatcher uses. This shouldn't change once assigned, 
so that's something for you to manage. Sorry.
Fields are additive only — each message carries the schema version it was encoded with so decoders can check
before reading anything newer.

Encoders write straight into a `ZBuffer` at known byte offsets and abort if it is too small.
`wrap()` zeroes only the fields that have no setter, so **every field with a setter must be set** —
an unset one carries whatever the buffer held before. (This will probably change in the future tbh)

Decoders never copy: they wrap a `ZBufferView` over the mapping and read fields in place.

## Writing an app

Consume the event stream by subclassing `EventStreamApp` and implementing `onEvent`. System events
(session start/end, time) go through a separate handler that returns a `SystemEventResult`; the
string views in it point into the event buffer and are only valid for that call.

Commands go out through `Commander`, which enforces one in flight at a time — the next one waits
until an event comes back carrying your app's `msgSender`. A command that goes unacknowledged past
`command_timeout_ns` is fatal by default - if an app can't figure out why it's command was rejected,
then the assumption is we're in a bad state so let's shut down shop.

Anything that needs to run on the app's thread implements `Pollable`; it's called on every poll cycle.

## Testing

`TestHarness` drives a sequencer and any number of apps against a `MockClock`, stepping them one
poll cycle at a time. Because the clock is synthetic and the event stream fixes the order, tests
are deterministic — no sleeps and no timing assertions.

Note that the harness calls `pollEvents()` and `pollPollables()` directly rather than going through
`App::run()`, so the real run loops are not covered by it.
