#include <zbuffer/zbuffer.hpp>

#include <cassert>
#include <cstring>
#include <iostream>

void test_zbuffer_basic_write_read() {
    std::cout << "  basic write/read...\n";
    
    zbuffer::ZBuffer buf(1024);
    
    assert(buf.capacity() == 1024);
    assert(buf.readable() == 0);
    assert(buf.writable() == 1024);
    assert(buf.empty());
    
    // Write string
    assert(buf.write("hello", 5));
    assert(buf.readable() == 5);
    assert(buf.writable() == 1024 - 5);
    
    // Read it back
    char str[6] = {};
    assert(buf.read(str, 5));
    assert(std::strcmp(str, "hello") == 0);
    assert(buf.empty());
}

void test_zbuffer_value_types() {
    std::cout << "  value types...\n";
    
    zbuffer::ZBuffer buf(256);
    
    // Write various types
    assert(buf.write_value<std::uint8_t>(0xAB));
    assert(buf.write_value<std::uint16_t>(0x1234));
    assert(buf.write_value<std::uint32_t>(0xDEADBEEF));
    assert(buf.write_value<std::uint64_t>(0x123456789ABCDEF0ULL));
    assert(buf.write_value<double>(3.14159));
    
    // Read them back
    std::uint8_t u8;
    std::uint16_t u16;
    std::uint32_t u32;
    std::uint64_t u64;
    double d;
    
    assert(buf.read_value(u8) && u8 == 0xAB);
    assert(buf.read_value(u16) && u16 == 0x1234);
    assert(buf.read_value(u32) && u32 == 0xDEADBEEF);
    assert(buf.read_value(u64) && u64 == 0x123456789ABCDEF0ULL);
    assert(buf.read_value(d) && d == 3.14159);
    assert(buf.empty());
}

void test_zbuffer_peek() {
    std::cout << "  peek (zero-copy)...\n";
    
    zbuffer::ZBuffer buf(256);
    buf.write("test data", 9);
    
    // Peek doesn't advance read position
    auto view1 = buf.peek_string();
    assert(view1 == "test data");
    assert(buf.readable() == 9);
    
    auto view2 = buf.peek_string();
    assert(view2 == "test data");
    assert(buf.readable() == 9);
    
    // Now consume
    buf.skip(4);
    auto view3 = buf.peek_string();
    assert(view3 == " data");
}

void test_zbuffer_reset() {
    std::cout << "  reset...\n";
    
    zbuffer::ZBuffer buf(256);
    buf.write("some data", 9);
    buf.skip(5);
    
    assert(buf.read_pos() == 5);
    assert(buf.write_pos() == 9);
    
    buf.reset();
    
    assert(buf.read_pos() == 0);
    assert(buf.write_pos() == 0);
    assert(buf.readable() == 0);
    assert(buf.writable() == 256);
}

void test_zbuffer_compact() {
    std::cout << "  compact...\n";
    
    zbuffer::ZBuffer buf(256);
    buf.write("AAAA", 4);
    buf.write("BBBB", 4);
    buf.write("CCCC", 4);
    
    // Consume first 8 bytes
    buf.skip(8);
    assert(buf.peek_string() == "CCCC");
    assert(buf.writable() == 256 - 12);
    
    // Compact moves remaining data to front
    buf.compact();
    
    assert(buf.read_pos() == 0);
    assert(buf.write_pos() == 4);
    assert(buf.peek_string() == "CCCC");
    assert(buf.writable() == 256 - 4);
}

void test_zbuffer_overflow_protection() {
    std::cout << "  overflow protection...\n";
    
    zbuffer::ZBuffer buf(16);
    
    // Fill it up
    assert(buf.write("1234567890123456", 16));
    assert(buf.writable() == 0);
    
    // Should fail - no space
    assert(!buf.write("x", 1));
    
    // Read some
    buf.skip(8);
    
    // Still can't write (write_pos at end)
    assert(!buf.write("x", 1));
    
    // After compact, can write again
    buf.compact();
    assert(buf.writable() == 8);
    assert(buf.write("XXXXXXXX", 8));
}

void test_zbuffer_span_interface() {
    std::cout << "  span interface...\n";
    
    zbuffer::ZBuffer buf(256);
    
    std::byte data[] = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    assert(buf.write(std::span<const std::byte>(data, 3)));
    
    auto span = buf.peek();
    assert(span.size() == 3);
    assert(span[0] == std::byte{0x01});
    assert(span[1] == std::byte{0x02});
    assert(span[2] == std::byte{0x03});
}

int main() {
    std::cout << "ZBuffer Tests:\n";
    
    test_zbuffer_basic_write_read();
    test_zbuffer_value_types();
    test_zbuffer_peek();
    test_zbuffer_reset();
    test_zbuffer_compact();
    test_zbuffer_overflow_protection();
    test_zbuffer_span_interface();
    
    std::cout << "ZBuffer: ALL PASSED\n\n";
    return 0;
}