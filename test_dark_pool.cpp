#include "luv_dark_pool.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

void test_price_display_time_priority() {
    luv::DarkPoolEngine dark_pool;
    uint16_t sym = 1;

    // Add 3 resting Sell orders at price 10000:
    // Order 1: Hidden, ts=100
    dark_pool.add_order(1, 10, sym, luv::exec::kSell, luv::DisplayMode::kHidden, 10000, 100, 0, 100);
    // Order 2: Displayed, ts=200
    dark_pool.add_order(2, 20, sym, luv::exec::kSell, luv::DisplayMode::kDisplayed, 10000, 100, 0, 200);
    // Order 3: Displayed, ts=300
    dark_pool.add_order(3, 30, sym, luv::exec::kSell, luv::DisplayMode::kDisplayed, 10000, 100, 0, 300);

    // Incoming aggressive Buy for 150 shares at 10000
    // Expected priority:
    // 1st: Order 2 (Displayed, ts=200) -> 100 shares
    // 2nd: Order 3 (Displayed, ts=300) -> 50 shares
    // Order 1 (Hidden) should NOT be matched yet because Displayed has priority!

    luv::MatchFill fills[10];
    size_t count = dark_pool.match_cross(
        99, 999, sym, luv::exec::kBuy, 10000, 150, 9990, 10010, fills, 10);

    assert(count == 2);
    assert(fills[0].maker_order_id == 2);
    assert(fills[0].match_qty == 100);
    assert(fills[1].maker_order_id == 3);
    assert(fills[1].match_qty == 50);

    std::printf("[PASS] test_price_display_time_priority\n");
}

void test_midpoint_pegging_and_maq() {
    luv::DarkPoolEngine dark_pool;
    uint16_t sym = 1;

    // Best Bid = 10000, Best Ask = 10020 -> Midpoint = 10010
    // Resting Midpoint peg with MAQ (MinQty = 500)
    dark_pool.add_order(10, 10, sym, luv::exec::kSell, luv::DisplayMode::kMidpointPeg, 0, 1000, 500, 100);

    luv::MatchFill fills[10];

    // Taker 1: Buy 200 shares (Below MAQ 500) -> Should NOT match!
    size_t count1 = dark_pool.match_cross(
        101, 999, sym, luv::exec::kBuy, 10050, 200, 10000, 10020, fills, 10);
    assert(count1 == 0);

    // Taker 2: Buy 600 shares (>= MAQ 500) -> Should match at midpoint 10010!
    size_t count2 = dark_pool.match_cross(
        102, 999, sym, luv::exec::kBuy, 10050, 600, 10000, 10020, fills, 10);
    assert(count2 == 1);
    assert(fills[0].maker_order_id == 10);
    assert(fills[0].match_price == 10010);
    assert(fills[0].match_qty == 600);
    assert(fills[0].is_midpoint);

    std::printf("[PASS] test_midpoint_pegging_and_maq\n");
}

int main() {
    test_price_display_time_priority();
    test_midpoint_pegging_and_maq();
    std::printf("All Dark Pool & Hidden liquidity tests passed successfully.\n");
    return 0;
}
