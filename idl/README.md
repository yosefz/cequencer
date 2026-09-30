# IDL Code Generator

Generates high-performance C++ message encoders/decoders from a simple schema definition.

## Quick Start

```bash
cd idl
python codegen.py schema.idl --output ../libs/messages/include/messages/
```

## Schema Format

```idl
# Comments start with #

# Enums
enum Side
    BUY
    SELL

# Objects (embedded structs)
object PriceLevel
    float64 price
    uint64 quantity

# Commands (request messages)
command NewOrderSingle
    string(16) clOrdId
    string(8) symbol
    Side side
    float64 price
    uint64 quantity

# Events (response/notification messages)
event Trade
    string(8) symbol
    float64 price
    uint64 quantity
    Side aggressor
```

## Types

| Type | C++ Type | Size |
|------|----------|------|
| `int8` | `std::int8_t` | 1 |
| `int16` | `std::int16_t` | 2 |
| `int32` | `std::int32_t` | 4 |
| `int64` | `std::int64_t` | 8 |
| `uint8` | `std::uint8_t` | 1 |
| `uint16` | `std::uint16_t` | 2 |
| `uint32` | `std::uint32_t` | 4 |
| `uint64` | `std::uint64_t` | 8 |
| `float32` | `float` | 4 |
| `float64` | `double` | 8 |
| `string(N)` | `std::array<char, N>` | N |
| `EnumName` | `EnumName` (enum class) | 4 |
| `ObjectName` | `ObjectName` (struct) | sum of fields |
| `list(T, N)` | `List<T, N>` | 4 + N * sizeof(T) |

## Default Message Fields

All messages automatically get these fields prepended:

```cpp
uint64 sequenceNumber    // Message sequence number
string(8) msgSender      // Sender identifier
uint64 sendingTimestamp  // Timestamp in nanoseconds
```

To add more default fields, edit `DEFAULT_MESSAGE_FIELDS` in `codegen.py`.

## Generated Code

The generator creates:

| File | Contents |
|------|----------|
| `types.hpp` | Enums, objects, List template |
| `messages.hpp` | Message classes with encode/decode |
| `registry.hpp` | Type IDs, variant, decode dispatcher |
| `all.hpp` | Umbrella include |

## Usage Example

```cpp
#include <messages/all.hpp>
#include <zbuffer/zbuffer.hpp>
#include <chronicle/chronicle.hpp>

// Create a message
messages::NewOrderSingle order;
order.sequenceNumber = 1;
std::strncpy(order.symbol.data(), "AAPL", 8);
order.side = messages::Side::BUY;
order.price = 150.25;
order.quantity = 100;

// Encode to ZBuffer
zbuffer::ZBuffer buf(1024);
order.encode(buf);

// Write to chronicle
auto appender = chronicle::Appender::create("orders.dat", 64*1024*1024);
appender.write(buf);

// Read back
auto tailer = chronicle::Tailer::open("orders.dat");
if (auto msg = tailer.poll()) {
    auto decoded = messages::NewOrderSingle::decode(msg);
    std::cout << "Price: " << decoded.price << "\n";
}
```

## Wire Format

Messages are fixed-size binary with fields laid out sequentially:

```
[sequenceNumber: 8][msgSender: 8][sendingTimestamp: 8][field1: N][field2: M]...
```

Lists have a 4-byte count prefix followed by fixed-capacity elements:

```
[count: 4][element0: S][element1: S]...[elementN-1: S]
```

Unused list slots are zeroed but still occupy space (fixed-size for predictable offsets).

## Performance

- **Zero-copy decode**: `decode(const std::byte*)` returns a struct with direct copies
- **No allocations**: All sizes known at compile time
- **Inline-friendly**: Simple memcpy operations that optimize well
- **Cache-friendly**: Sequential memory layout

## Regenerating

After editing `schema.idl`:

```bash
python codegen.py schema.idl -o ../libs/messages/include/messages/
```

Then rebuild in CLion (**Build > Rebuild Project** or `Ctrl+Shift+F9`).