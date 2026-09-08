#include "luv_cip_forward_points.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Covered Interest Parity (CIP) Engine Tests..." << std::endl;

    luv::CIPForwardPointsEngine engine;

    // 1. Case 1: Parity Equilibrium (No Arb)
    // S = 1.0850, r_d (USD) = 5.25%, r_f (EUR) = 3.75%, d = 90 days (0.25 years)
    // F_theo = 1.0850 * (1 + 0.0525 * 0.25) / (1 + 0.0375 * 0.25)
    // F_theo = 1.0850 * (1.013125 / 1.009375) = 1.08903
    // Theoretical points = (1.08903 - 1.0850) * 10000 = ~40.3 pips
    luv::CIPMarketInput input1{};
    std::strncpy(input1.ccy_pair, "EURUSD", 6);
    input1.spot_price = 1.0850;
    input1.domestic_rate = 0.0525;
    input1.foreign_rate = 0.0375;
    input1.days_to_maturity = 90;
    input1.market_forward_points = 40.3; // Market in parity
    input1.bid_ask_spread_pips = 0.5;
    input1.haircut_rate = 0.0002;

    auto res1 = engine.analyze_parity(input1);
    std::cout << "  [Equilibrium] Theo Points: " << res1.theoretical_forward_points 
              << ", Basis Spread: " << res1.basis_spread_bp << " bps" << std::endl;

    assert(!res1.arbitrage_opportunity);
    assert(std::abs(res1.basis_spread_bp) < 2.0);

    // 2. Case 2: Severe Negative Cross-Currency Basis (Forward points depressed to 10.0 pips)
    // Large demand to borrow USD synthetic via FX swap
    luv::CIPMarketInput input2 = input1;
    input2.market_forward_points = 10.0; // 30 pips below theoretical parity!

    auto res2 = engine.analyze_parity(input2);
    std::cout << "  [Negative Basis Dislocation] Basis Spread: " << res2.basis_spread_bp 
              << " bps, Net Profit: " << res2.net_arbitrage_profit_bp << " bps" << std::endl;

    assert(res2.arbitrage_opportunity);
    assert(res2.basis_spread_bp < -50.0); // Significant negative basis
    assert(res2.borrow_domestic_lend_foreign); // Borrow USD, lend synthetic EUR
    assert(res2.net_arbitrage_profit_bp > 20.0);

    // 3. Case 3: Positive Basis Dislocation (Forward points elevated to 80.0 pips)
    luv::CIPMarketInput input3 = input1;
    input3.market_forward_points = 80.0; // 40 pips above parity

    auto res3 = engine.analyze_parity(input3);
    std::cout << "  [Positive Basis Dislocation] Basis Spread: " << res3.basis_spread_bp 
              << " bps, Net Profit: " << res3.net_arbitrage_profit_bp << " bps" << std::endl;

    assert(res3.arbitrage_opportunity);
    assert(res3.basis_spread_bp > 50.0);
    assert(!res3.borrow_domestic_lend_foreign); // Reverse flow

    std::cout << "[PASS] Covered Interest Parity (CIP) Engine Tests Passed!" << std::endl;
    return 0;
}
