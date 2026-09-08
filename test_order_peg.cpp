#include <iostream>
#include <cassert>
#include "luv_order_peg.hpp"

int main() {
    std::cout << "[TEST] Running Pegged Orders Engine Test...\n";

    // NBBO: Bid = $100.00 (1,000,000), Ask = $100.10 (1,001,000)
    uint64_t bid = 1'000'000;
    uint64_t ask = 1'001'000;

    // 1. Midpoint Peg
    luv::PeggedOrder mid_order;
    mid_order.order_id = 1;
    mid_order.is_buy = true;
    mid_order.peg_type = luv::PegType::Midpoint;
    mid_order.offset_ticks = 0;
    
    assert(luv::PeggingEngine::update_order_price(mid_order, bid, ask));
    assert(mid_order.current_effective_price == 1'000'500); // $100.05

    // 2. Primary Peg Buy with +$0.01 offset
    luv::PeggedOrder prim_buy;
    prim_buy.order_id = 2;
    prim_buy.is_buy = true;
    prim_buy.peg_type = luv::PegType::Primary;
    prim_buy.offset_ticks = 100; // +$0.01
    prim_buy.limit_cap = 1'000'500; // Cap at $100.05

    assert(luv::PeggingEngine::update_order_price(prim_buy, bid, ask));
    assert(prim_buy.current_effective_price == 1'000'100); // $100.01

    // 3. Primary Peg Buy exceeding cap
    bid = 1'000'600; // Bid moves to $100.06
    ask = 1'000'800;
    assert(luv::PeggingEngine::update_order_price(prim_buy, bid, ask));
    assert(prim_buy.current_effective_price == 1'000'500); // Capped at $100.05

    // 4. Market Peg Sell with -$0.02 offset
    luv::PeggedOrder mkt_sell;
    mkt_sell.order_id = 3;
    mkt_sell.is_buy = false;
    mkt_sell.peg_type = luv::PegType::Market;
    mkt_sell.offset_ticks = -200; // -$0.02 from opposite side (bid)
    
    assert(luv::PeggingEngine::update_order_price(mkt_sell, bid, ask));
    assert(mkt_sell.current_effective_price == 1'000'400); // $100.06 - $0.02 = $100.04

    std::cout << "[TEST] Pegged Orders Engine Test Passed!\n";
    return 0;
}
