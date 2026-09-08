#include "luv_sec_form_nport.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting SEC Form N-PORT / Rule 22e-4 Liquidity Engine Tests..." << std::endl;

    luv::FormNPORTLiquidityEngine engine;

    // 1. Position 1: Mega-cap US Equity ($10M value, $500M ADV) -> Highly Liquid
    luv::NPORTPositionRecord p1{};
    std::strncpy(p1.identifier, "AAPL", 4);
    p1.market_value_usd = 10'000'000.0;
    p1.daily_volume_usd = 500'000'000.0; // Capacity at 10% = $50M/day -> 0.2 days to liquidate
    p1.bid_ask_spread_pct = 0.0001;
    p1.is_restricted_security = false;
    assert(engine.add_position(p1));

    // 2. Position 2: US Treasury Bill ($30M value, $1B ADV) -> Highly Liquid
    luv::NPORTPositionRecord p2{};
    std::strncpy(p2.identifier, "TBILL_3M", 8);
    p2.market_value_usd = 30'000'000.0;
    p2.daily_volume_usd = 1'000'000'000.0;
    p2.bid_ask_spread_pct = 0.00005;
    assert(engine.add_position(p2));

    // 3. Position 3: Mid-cap Equity ($5M value, $10M ADV) -> Moderately Liquid (5 days to liquidate at 10% ADV)
    luv::NPORTPositionRecord p3{};
    std::strncpy(p3.identifier, "MIDCAP1", 7);
    p3.market_value_usd = 5'000'000.0;
    p3.daily_volume_usd = 10'000'000.0; // 10% ADV = $1M/day -> 5 days
    p3.bid_ask_spread_pct = 0.001;
    assert(engine.add_position(p3));

    // 4. Position 4: Small-cap Equity ($4M value, $3M ADV) -> Less Liquid (13.3 days at 10% ADV)
    luv::NPORTPositionRecord p4{};
    std::strncpy(p4.identifier, "SMALLCAP1", 9);
    p4.market_value_usd = 4'000'000.0;
    p4.daily_volume_usd = 3'000'000.0; // 10% ADV = $300k/day -> 13.3 days
    p4.bid_ask_spread_pct = 0.005;
    assert(engine.add_position(p4));

    // 5. Position 5: Restricted Private Placement Bond ($1M value) -> Illiquid
    luv::NPORTPositionRecord p5{};
    std::strncpy(p5.identifier, "RESTRICTED1", 11);
    p5.market_value_usd = 1'000'000.0;
    p5.daily_volume_usd = 0.0;
    p5.is_restricted_security = true;
    assert(engine.add_position(p5));

    assert(engine.position_count() == 5);
    auto summary = engine.generate_summary();

    std::cout << "  Total NAV:            $" << summary.total_net_asset_value << std::endl;
    std::cout << "  Highly Liquid:        " << summary.highly_liquid_pct << "%" << std::endl;
    std::cout << "  Moderately Liquid:    " << summary.moderately_liquid_pct << "%" << std::endl;
    std::cout << "  Less Liquid:          " << summary.less_liquid_pct << "%" << std::endl;
    std::cout << "  Illiquid:             " << summary.illiquid_pct << "%" << std::endl;

    assert(summary.total_net_asset_value == 50'000'000.0);
    // Highly Liquid = ($10M + $30M) / $50M = 80.0%
    assert(std::abs(summary.highly_liquid_pct - 80.0) < 0.01);
    // Moderately Liquid = $5M / $50M = 10.0%
    assert(std::abs(summary.moderately_liquid_pct - 10.0) < 0.01);
    // Less Liquid = $4M / $50M = 8.0%
    assert(std::abs(summary.less_liquid_pct - 8.0) < 0.01);
    // Illiquid = $1M / $50M = 2.0%
    assert(std::abs(summary.illiquid_pct - 2.0) < 0.01);

    // Verify Rule 22e-4 limits (2% < 15% ceiling, 80% > 50% HLIM)
    assert(!summary.illiquid_limit_breached);
    assert(!summary.hlim_breached);

    std::cout << "[PASS] SEC Form N-PORT / Rule 22e-4 Liquidity Engine Tests Passed!" << std::endl;
    return 0;
}
