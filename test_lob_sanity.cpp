#include <cassert>
#include <cstdio>
#include <cstring>
#include "luv_arena.hpp"
#include "luv_decode_itch.hpp"
#include "luv_lob.hpp"

using namespace luv;

void test_regulatory_and_cross_itch_messages() {
    std::printf("[test_regulatory_and_cross_itch_messages] Running...\n");
    SymbolTable symbols;
    const std::array<char, 8> ticker = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
    assert(symbols.insert(ticker.data(), 5));

    TickMsg out{};

    // 1. Test Cross Trade 'Q' (40 bytes)
    std::array<uint8_t, itch::kLenCrossTrade> q_raw{};
    q_raw[0] = itch::kCrossTrade;
    // timestamp: offset 5..10
    q_raw[5] = 0; q_raw[6] = 0; q_raw[7] = 0; q_raw[8] = 0; q_raw[9] = 1; q_raw[10] = 0;
    // shares: offset 11..18 (uint64 be)
    q_raw[18] = 50;
    // stock ticker: offset 19..26
    std::memcpy(q_raw.data() + 19, ticker.data(), 8);
    // price: offset 27..30 (uint32 be)
    q_raw[30] = 150;
    // match num: offset 31..38
    q_raw[38] = 99;

    assert(decode_itch(q_raw.data(), q_raw.size(), symbols, out));
    assert(out.symbol_idx == 5);
    assert(out.qty == 50);
    assert(out.price == 150);
    assert((out.flags & tick_flags::kCross) != 0);
    assert((out.flags & tick_flags::kPrintable) != 0);

    // 2. Test Stock Trading Action / Halt 'H' (25 bytes)
    std::array<uint8_t, itch::kLenStockTradingAction> h_raw{};
    h_raw[0] = itch::kStockTradingAction;
    std::memcpy(h_raw.data() + 11, ticker.data(), 8);
    h_raw[19] = 'H'; // Halted

    assert(decode_itch(h_raw.data(), h_raw.size(), symbols, out));
    assert(out.symbol_idx == 5);
    assert((out.flags & tick_flags::kHalted) != 0);

    // 3. Test Reg SHO Short Sale Restriction 'Y' (20 bytes)
    std::array<uint8_t, itch::kLenRegSHO> y_raw{};
    y_raw[0] = itch::kRegSHO;
    std::memcpy(y_raw.data() + 11, ticker.data(), 8);
    y_raw[19] = '1'; // Short sale price test restriction in effect

    assert(decode_itch(y_raw.data(), y_raw.size(), symbols, out));
    assert(out.symbol_idx == 5);
    assert((out.flags & tick_flags::kShortSaleRestricted) != 0);

    std::printf("[test_regulatory_and_cross_itch_messages] PASSED\n");
}

void test_lob_crossed_and_stale_detection() {
    std::printf("[test_lob_crossed_and_stale_detection] Running...\n");
    Arena arena;
    assert(arena.init());

    LOBEngine lob;
    assert(lob.init(arena));

    const uint16_t sym = 0;
    uint64_t now_ns = 1'000'000'000ULL;

    // Add Bid at $100
    TickMsg bid_tick{};
    bid_tick.msg_type = itch::kAddOrder;
    bid_tick.symbol_idx = sym;
    bid_tick.order_ref = 1001;
    bid_tick.flags = tick_flags::kBuy;
    bid_tick.qty = 100;
    bid_tick.price = 100 * 10'000;
    bid_tick.timestamp = now_ns;
    lob.process(bid_tick);

    // Add Ask at $105
    TickMsg ask_tick{};
    ask_tick.msg_type = itch::kAddOrder;
    ask_tick.symbol_idx = sym;
    ask_tick.order_ref = 1002;
    ask_tick.flags = 0; // Sell
    ask_tick.qty = 100;
    ask_tick.price = 105 * 10'000;
    ask_tick.timestamp = now_ns;
    lob.process(ask_tick);

    // Normal book state: not crossed
    assert(!lob.is_crossed(sym));
    assert(lob.best_bid_price(sym) == 100 * 10'000);
    assert(lob.best_ask_price(sym) == 105 * 10'000);
    assert(lob.spread(sym) == 5 * 10'000);

    // Staleness check
    assert(!lob.is_stale(sym, now_ns + 1'000'000'000ULL, 5'000'000'000ULL)); // 1s < 5s timeout
    assert(lob.is_stale(sym, now_ns + 6'000'000'000ULL, 5'000'000'000ULL)); // 6s > 5s timeout

    // Add aggressive Bid at $106 causing crossed market (Bid $106 >= Ask $105)
    TickMsg crossed_bid{};
    crossed_bid.msg_type = itch::kAddOrder;
    crossed_bid.symbol_idx = sym;
    crossed_bid.order_ref = 1003;
    crossed_bid.flags = tick_flags::kBuy;
    crossed_bid.qty = 100;
    crossed_bid.price = 106 * 10'000;
    crossed_bid.timestamp = now_ns + 100;
    lob.process(crossed_bid);

    assert(lob.is_crossed(sym));
    assert(lob.best_bid_price(sym) == 106 * 10'000);
    assert(lob.best_ask_price(sym) == 105 * 10'000);

    std::printf("[test_lob_crossed_and_stale_detection] PASSED\n");
}

int main() {
    test_regulatory_and_cross_itch_messages();
    test_lob_crossed_and_stale_detection();
    std::printf("ALL LOB SANITY TESTS PASSED\n");
    return 0;
}
