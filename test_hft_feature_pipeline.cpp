#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_hft_feature_pipeline.hpp"

int main() {
    std::cout << "[TEST] Running HFT Microstructure Feature Pipeline Test...\n";

    luv::MicrostructureFeatureEngine engine;

    // 1. Initial quote: Bid $100.00 x 100, Ask $100.10 x 300
    engine.on_quote(1'000'000, 100, 1'001'000, 300);
    const auto& f1 = engine.get_features();

    // Microprice should tilt towards ask because bid quantity is smaller, or vice versa
    // Microprice = (100.00 * 300 + 100.10 * 100) / 400 = 40010 / 400 = 100.025
    assert(std::abs(f1.micro_price - 1'000'250.0) < 1.0);
    assert(std::abs(f1.book_imbalance - (-0.5)) < 1e-4); // (100 - 300) / 400 = -0.5
    assert(f1.spread_bps > 9.9 && f1.spread_bps < 10.1); // ~10 bps

    // 2. Second quote with increase in bid size from 100 to 250 (same prices)
    engine.on_quote(1'000'000, 250, 1'001'000, 300);
    const auto& f2 = engine.get_features();
    // OFI = delta_bid - delta_ask = (250 - 100) - (300 - 300) = +150
    assert(std::abs(f2.ofi - 150.0) < 1e-4);

    // 3. Trade updates
    engine.on_trade(true, 500); // 500 buy
    engine.on_trade(false, 100); // 100 sell
    const auto& f3 = engine.get_features();
    // Trade imbalance = (500 - 100) / 600 = +0.6666...
    assert(std::abs(f3.trade_imbalance - (400.0 / 600.0)) < 1e-4);

    std::cout << "[TEST] HFT Microstructure Feature Pipeline Test Passed!\n";
    return 0;
}
