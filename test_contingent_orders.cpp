#include <iostream>
#include <cassert>
#include "luv_contingent_orders.hpp"

int main() {
    std::cout << "[TEST] Running Multi-Leg Contingent & Bracket Order Engine Test...\n";

    luv::ContingentOrderManager manager;

    // 1. Test OCO Pair: Take-Profit Limit (101) @ $110.00 vs Stop-Loss Stop (102) @ $95.00
    assert(manager.add_oco_pair(101, 1'100'000, 102, 950'000, false, 100));

    assert(manager.get_order(101)->state == luv::ContingentState::Active);
    assert(manager.get_order(102)->state == luv::ContingentState::Active);

    // Limit fills -> Stop must be cancelled automatically
    manager.on_order_fill(101);
    assert(manager.get_order(101)->state == luv::ContingentState::Filled);
    assert(manager.get_order(102)->state == luv::ContingentState::Cancelled);

    // 2. Test Trailing Stop: Initial price $100.00, trailing delta $2.00 (20,000)
    assert(manager.add_trailing_stop(201, false, 1'000'000, 20'000, 50));
    const auto* ts = manager.get_order(201);
    assert(ts->state == luv::ContingentState::Active);
    assert(ts->stop_price == 980'000); // $98.00

    // Price climbs to $105.00 -> Stop ratchets up to $103.00
    manager.on_market_price(1'050'000);
    assert(manager.get_order(201)->peak_price == 1'050'000);
    assert(manager.get_order(201)->stop_price == 1'030'000);
    assert(manager.get_order(201)->state == luv::ContingentState::Active);

    // Price dips slightly to $104.00 -> Stop remains at $103.00
    manager.on_market_price(1'040'000);
    assert(manager.get_order(201)->stop_price == 1'030'000);
    assert(manager.get_order(201)->state == luv::ContingentState::Active);

    // Price plunges to $102.50 (< $103.00) -> Trailing Stop Triggered
    manager.on_market_price(1'025'000);
    assert(manager.get_order(201)->state == luv::ContingentState::Triggered);

    std::cout << "[TEST] Multi-Leg Contingent & Bracket Order Engine Test Passed!\n";
    return 0;
}
