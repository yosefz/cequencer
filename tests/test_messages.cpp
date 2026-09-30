#include <schema/all.hpp>
#include <zbuffer/zbuffer.hpp>
#include <holocron/holocronQueue.hpp>

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

// Convenience aliases for our schema namespaces
namespace enums = messages::schema::enums;
namespace objects = messages::schema::objects;
namespace commands = messages::schema::commands;
namespace events = messages::schema::events;

namespace {

std::filesystem::path test_dir() {
    auto dir = std::filesystem::current_path() / "test_output";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string temp_path(const char* name) {
    return (test_dir() / (std::string("msg_") + name + ".dat")).string();
}

void cleanup(const std::string& path) {
    std::filesystem::remove(path);
}

} // anonymous namespace

void test_message_sizes() {
    std::cout << "  message sizes...\n";

    // Verify expected sizes (base fields = 50 bytes: messageVersion(2) + sequenceNumber(8) + msgSender(32) + sendingTimestamp(8))
    static_assert(commands::NewOrderSingleCommandDecoder::SIZE == 102);  // 50 + 52
    static_assert(commands::CancelOrderCommandDecoder::SIZE == 90);      // 50 + 40
    static_assert(commands::ModifyOrderCommandDecoder::SIZE == 106);     // 50 + 56
    static_assert(events::ExecutionReportEventDecoder::SIZE == 154);     // 50 + 104
    static_assert(events::TradeEventDecoder::SIZE == 86);                // 50 + 36
    static_assert(events::MarketDataEventDecoder::SIZE == 474);          // 50 + 424
}

void test_new_order_single() {
    std::cout << "  NewOrderSingleCommand encode/decode...\n";

    zbuffer::ZBuffer buf(1024);

    // Encode
    commands::NewOrderSingleCommandEncoder encoder;
    encoder.wrap(buf);
    encoder.setSequenceNumber(12345);
    encoder.setMsgSender("TRADER1");
    encoder.setSendingTimestamp(1700000000000);
    encoder.setClOrdId("ORD-001");
    encoder.setSymbol("AAPL");
    encoder.setSide(enums::Side::BUY);
    encoder.setOrderType(enums::OrderType::LIMIT);
    encoder.setPrice(150.25);
    encoder.setQuantity(100);
    encoder.setTimeInForce(enums::TimeInForce::DAY);
    encoder.commit();

    // Decode
    buf.set_read_pos(0);
    commands::NewOrderSingleCommandDecoder decoder;
    decoder.wrap(buf);

    assert(decoder.getSequenceNumber() == 12345);
    assert(decoder.getMsgSenderAsString() == "TRADER1");
    assert(decoder.getSendingTimestamp() == 1700000000000);
    assert(decoder.getClOrdIdAsString() == "ORD-001");
    assert(decoder.getSymbolAsString() == "AAPL");
    assert(decoder.getSide() == enums::Side::BUY);
    assert(decoder.getOrderType() == enums::OrderType::LIMIT);
    assert(decoder.getPrice() == 150.25);
    assert(decoder.getQuantity() == 100);
    assert(decoder.getTimeInForce() == enums::TimeInForce::DAY);
}

void test_zbuffer_encode_decode() {
    std::cout << "  ZBuffer encode/decode...\n";

    zbuffer::ZBuffer buf(1024);

    // Encode
    events::TradeEventEncoder encoder;
    encoder.wrap(buf);
    encoder.setSequenceNumber(1);
    encoder.setMsgSender("EXCH");
    encoder.setSendingTimestamp(1700000001000);
    encoder.setSymbol("MSFT");
    encoder.setPrice(380.50);
    encoder.setQuantity(50);
    encoder.setAggressor(enums::Side::SELL);
    encoder.setTradeId(999888777);
    encoder.commit();

    assert(buf.write_pos() == events::TradeEventDecoder::SIZE);

    // Decode
    buf.set_read_pos(0);
    events::TradeEventDecoder decoder;
    decoder.wrap(buf);

    assert(decoder.getSequenceNumber() == 1);
    assert(decoder.getPrice() == 380.50);
    assert(decoder.getQuantity() == 50);
    assert(decoder.getAggressor() == enums::Side::SELL);
    assert(decoder.getTradeId() == 999888777);
}

void test_zbuffer_slice() {
    std::cout << "  ZBuffer slice for string fields...\n";

    zbuffer::ZBuffer buf(1024);

    // Encode
    events::ExecutionReportEventEncoder encoder;
    encoder.wrap(buf);
    encoder.setSequenceNumber(42);
    encoder.setMsgSender("OMS");
    encoder.setSymbol("GOOG");
    encoder.setClOrdId("CLO-123");
    encoder.commit();

    // Decode and get ZBuffer slices
    buf.set_read_pos(0);
    events::ExecutionReportEventDecoder decoder;
    decoder.wrap(buf);

    // getMsgSender() returns ZBuffer slice
    auto senderBuf = decoder.getMsgSender();
    assert(senderBuf.capacity() == 32);  // Fixed field size
    assert(senderBuf.asString() == "OMS");

    // getSymbol() returns ZBuffer slice
    auto symbolBuf = decoder.getSymbol();
    assert(symbolBuf.capacity() == 8);
    assert(symbolBuf.asString() == "GOOG");

    // AsString() convenience method
    assert(decoder.getMsgSenderAsString() == "OMS");
    assert(decoder.getSymbolAsString() == "GOOG");
    assert(decoder.getClOrdIdAsString() == "CLO-123");
}

void test_serialization() {
    std::cout << "  serialization...\n";

    zbuffer::ZBuffer buf(1024);

    events::TradeEventEncoder encoder;
    encoder.wrap(buf);
    encoder.setSequenceNumber(123);
    encoder.setMsgSender("TEST");
    encoder.setSendingTimestamp(1700000000000);
    encoder.setSymbol("AAPL");
    encoder.setPrice(150.25);
    encoder.setQuantity(100);
    encoder.setAggressor(enums::Side::BUY);
    encoder.setTradeId(999);
    encoder.commit();

    buf.set_read_pos(0);
    events::TradeEventDecoder decoder;
    decoder.wrap(buf);

    std::ostringstream oss;
    decoder.serialize(oss);

    std::string result = oss.str();
    assert(result.find("TradeEvent{") != std::string::npos);
    assert(result.find("sequenceNumber=123") != std::string::npos);
    assert(result.find("symbol=\"AAPL\"") != std::string::npos);
    assert(result.find("price=150.25") != std::string::npos);
}

void test_chronicle_integration() {
    std::cout << "  Chronicle integration...\n";

    auto path = temp_path("chronicle");
    cleanup(path);

    // Write messages; TYPE_ID travels in the message header (as sequencer does)
    {
        auto appender = chronicle::Appender::create(path, 8192);
        zbuffer::ZBuffer buf(1024);

        // Write an order

        commands::NewOrderSingleCommandEncoder encoder;
        encoder.wrap(buf);
        encoder.setSequenceNumber(1);
        encoder.setMsgSender("TEST");
        encoder.setSendingTimestamp(1700000000000);
        encoder.setClOrdId("CLO-1");
        encoder.setSymbol("IBM");
        encoder.setSide(enums::Side::BUY);
        encoder.setOrderType(enums::OrderType::MARKET);
        encoder.setPrice(0);
        encoder.setQuantity(500);
        encoder.setTimeInForce(enums::TimeInForce::IOC);
        encoder.commit();
        appender.write(buf, commands::NewOrderSingleCommandEncoder::TYPE_ID);

        // Write a trade
        buf.reset();

        events::TradeEventEncoder tradeEncoder;
        tradeEncoder.wrap(buf);
        tradeEncoder.setSequenceNumber(2);
        tradeEncoder.setMsgSender("TEST");
        tradeEncoder.setSendingTimestamp(1700000001000);
        tradeEncoder.setSymbol("IBM");
        tradeEncoder.setPrice(145.00);
        tradeEncoder.setQuantity(500);
        tradeEncoder.setAggressor(enums::Side::BUY);
        tradeEncoder.setTradeId(12345);
        tradeEncoder.commit();
        appender.write(buf, events::TradeEventEncoder::TYPE_ID);
    }

    // Read messages
    {
        auto tailer = chronicle::Tailer::open(path);

        // Read order
        auto msg1 = tailer.poll();
        assert(msg1);
        assert(msg1.length() == commands::NewOrderSingleCommandDecoder::SIZE);
        assert(msg1.type_id() == commands::NewOrderSingleCommandDecoder::TYPE_ID);

        zbuffer::ZBuffer buf1(msg1.length());
        buf1.write(msg1.data(), msg1.length());
        buf1.set_read_pos(0);

        commands::NewOrderSingleCommandDecoder decoder1;
        decoder1.wrap(buf1);
        assert(decoder1.getSequenceNumber() == 1);
        assert(decoder1.getQuantity() == 500);
        assert(decoder1.getSymbolAsString() == "IBM");

        // Read trade
        auto msg2 = tailer.poll();
        assert(msg2);
        assert(msg2.length() == events::TradeEventDecoder::SIZE);
        assert(msg2.type_id() == events::TradeEventDecoder::TYPE_ID);

        zbuffer::ZBuffer buf2(msg2.length());
        buf2.write(msg2.data(), msg2.length());
        buf2.set_read_pos(0);

        events::TradeEventDecoder decoder2;
        decoder2.wrap(buf2);
        assert(decoder2.getSequenceNumber() == 2);
        assert(decoder2.getPrice() == 145.00);
        assert(decoder2.getTradeId() == 12345);

        assert(!tailer.poll());
    }

    cleanup(path);
}

void test_type_ids() {
    std::cout << "  type IDs...\n";

    assert(commands::NewOrderSingleCommandDecoder::TYPE_ID == 1);
    assert(commands::CancelOrderCommandDecoder::TYPE_ID == 2);
    assert(commands::ModifyOrderCommandDecoder::TYPE_ID == 3);
    assert(events::StartOfSessionEventDecoder::TYPE_ID == 4);
    assert(events::TimeEventDecoder::TYPE_ID == 5);
    assert(events::EndOfSessionEventDecoder::TYPE_ID == 6);
    assert(events::ExecutionReportEventDecoder::TYPE_ID == 7);
    assert(events::MarketDataEventDecoder::TYPE_ID == 8);
    assert(events::TradeEventDecoder::TYPE_ID == 9);

    // Test KIND flags
    assert(commands::NewOrderSingleCommandDecoder::IS_COMMAND == true);
    assert(commands::NewOrderSingleCommandDecoder::IS_EVENT == false);
    assert(events::TradeEventDecoder::IS_COMMAND == false);
    assert(events::TradeEventDecoder::IS_EVENT == true);
}

int main() {
    std::cout << "Messages Tests:\n";

    test_message_sizes();
    test_new_order_single();
    test_zbuffer_encode_decode();
    test_zbuffer_slice();
    test_serialization();
    test_chronicle_integration();
    test_type_ids();

    std::cout << "Messages: ALL PASSED\n\n";
    return 0;
}