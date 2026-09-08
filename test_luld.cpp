#include <iostream>
#include <cassert>
#include "luv_luld.hpp"

int main() {
    std::cout << "[TEST] Running SEC LULD Regulation Engine Test...\n";

    luv::LuldConfig config;
    config.tier = luv::LuldTier::Tier1;
    config.straddle_timeout_ns = 15'000'000'000ULL; // 15 seconds

    luv::LuldEngine luld(config);
    // Ref price $100.00 = 1,000,000 (scaled x10,000)
    luld.set_reference_price(1'000'000);

    const auto& b = luld.get_bands();
    // 5% band on $100 -> Lower: $95.00 (950,000), Upper: $105.00 (1,050,000)
    assert(b.lower_band == 950'000);
    assert(b.upper_band == 1'050'000);
    assert(b.state == luv::LuldState::Normal);

    // Trade within band allowed
    assert(luld.is_trade_allowed(1'000'000));
    assert(luld.is_trade_allowed(950'000));
    assert(luld.is_trade_allowed(1'050'000));
    assert(!luld.is_trade_allowed(949'999));
    assert(!luld.is_trade_allowed(1'050'001));

    // Update NBBO inside bands
    luld.update_nbbo(980'000, 1'020'000, 1'000'000'000ULL);
    assert(luld.get_bands().state == luv::LuldState::Normal);

    // Limit state: bid hits upper band ($105.00)
    luld.update_nbbo(1'050'000, 1'051'000, 2'000'000'000ULL);
    assert(luld.get_bands().state == luv::LuldState::LimitState);
    assert(!luld.get_bands().pause_triggered);

    // 10 seconds later: still in limit state (timeout is 15s)
    luld.update_nbbo(1'050'000, 1'051'000, 12'000'000'000ULL);
    assert(luld.get_bands().state == luv::LuldState::LimitState);
    assert(!luld.get_bands().pause_triggered);

    // 16 seconds later (>15s): trading pause triggered
    luld.update_nbbo(1'050'000, 1'051'000, 18'000'000'000ULL);
    assert(luld.get_bands().state == luv::LuldState::TradingPause);
    assert(luld.get_bands().pause_triggered);
    assert(!luld.is_trade_allowed(1'000'000));

    // Resume trading with new reference price $106.00
    luld.resume_trading(1'060'000);
    assert(luld.get_bands().state == luv::LuldState::Normal);
    assert(!luld.get_bands().pause_triggered);
    assert(luld.is_trade_allowed(1'060'000));

    std::cout << "[TEST] SEC LULD Regulation Engine Test Passed!\n";
    return 0;
}
