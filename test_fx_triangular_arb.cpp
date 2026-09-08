#include "luv_fx_triangular_arb.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Cross-Currency FX Triangular Arbitrage Tests..." << std::endl;

    luv::FXTriangularArbEngine arb_engine;

    // Case 1: Arbitrage Equilibrium / Normal Market (No Arb Opportunity)
    // EURUSD = 1.0850 / 1.0851
    // USDJPY = 155.00 / 155.02
    // Implied EURJPY = 1.0850 * 155.00 = 168.175
    // Market EURJPY = 168.17 / 168.19
    luv::FXQuote eurusd{};
    std::strncpy(eurusd.pair, "EURUSD", 6);
    eurusd.bid = 1.0850;
    eurusd.ask = 1.0851;
    eurusd.bid_size = 1'000'000;
    eurusd.ask_size = 1'000'000;
    eurusd.fee_rate = 0.0001;
    eurusd.est_slippage = 0.00005;

    luv::FXQuote eurjpy{};
    std::strncpy(eurjpy.pair, "EURJPY", 6);
    eurjpy.bid = 168.17;
    eurjpy.ask = 168.19;
    eurjpy.bid_size = 1'000'000;
    eurjpy.ask_size = 1'000'000;
    eurjpy.fee_rate = 0.0001;
    eurjpy.est_slippage = 0.00005;

    luv::FXQuote usdjpy{};
    std::strncpy(usdjpy.pair, "USDJPY", 6);
    usdjpy.bid = 155.00;
    usdjpy.ask = 155.02;
    usdjpy.bid_size = 1'000'000;
    usdjpy.ask_size = 1'000'000;
    usdjpy.fee_rate = 0.0001;
    usdjpy.est_slippage = 0.00005;

    auto opp1 = arb_engine.evaluate_eur_jpy_usd_triangle(eurusd, eurjpy, usdjpy);
    assert(!opp1.opportunity_found);

    // Case 2: Dislocated Market - Significant Forward Triangular Arbitrage Dislocation
    // EURJPY bid spikes up to 169.20 (Mispriced cross rate)
    luv::FXQuote eurjpy_dislocated = eurjpy;
    eurjpy_dislocated.bid = 169.20; // High EURJPY bid
    eurjpy_dislocated.ask = 169.22;

    auto opp2 = arb_engine.evaluate_eur_jpy_usd_triangle(eurusd, eurjpy_dislocated, usdjpy);
    std::cout << "  Opportunity Found: " << (opp2.opportunity_found ? "YES" : "NO") << std::endl;
    std::cout << "  Net Profit BP:     " << opp2.net_profit_bp << " bps" << std::endl;
    std::cout << "  Gross Multiplier:  " << opp2.gross_multiplier << std::endl;

    assert(opp2.opportunity_found);
    assert(opp2.net_profit_bp >= 50.0); // > 50 bps net profit
    assert(opp2.max_base_notional > 0.0);
    assert(std::strcmp(opp2.legs[0].pair, "EURUSD") == 0);
    assert(std::strcmp(opp2.legs[1].pair, "EURJPY") == 0);
    assert(std::strcmp(opp2.legs[2].pair, "USDJPY") == 0);

    // Case 3: Reverse Arbitrage Dislocation (USD -> JPY -> EUR -> USD)
    // EURJPY drops significantly to 167.00
    luv::FXQuote eurjpy_cheap = eurjpy;
    eurjpy_cheap.bid = 166.80;
    eurjpy_cheap.ask = 166.85;

    auto opp3 = arb_engine.evaluate_eur_jpy_usd_triangle(eurusd, eurjpy_cheap, usdjpy);
    std::cout << "  Reverse Arb Found: " << (opp3.opportunity_found ? "YES" : "NO") << std::endl;
    std::cout << "  Reverse Profit BP: " << opp3.net_profit_bp << " bps" << std::endl;
    assert(opp3.opportunity_found);
    assert(opp3.net_profit_bp >= 50.0);
    assert(std::strcmp(opp3.legs[0].pair, "USDJPY") == 0);

    std::cout << "[PASS] Cross-Currency FX Triangular Arbitrage Tests Passed!" << std::endl;
    return 0;
}
