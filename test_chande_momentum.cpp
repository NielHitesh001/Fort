#include "luv_chande_momentum.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Chande Momentum Oscillator (CMO) Tests..." << std::endl;

    luv::ChandeMomentumOscillator cmo(10, 0.5);

    // 1. Warm-up period (first 10 points)
    for (int i = 0; i < 10; ++i) {
        auto res = cmo.update(100.0 + i * 0.1);
        assert(!res.is_valid);
    }

    // 2. Pure Bullish Trend (prices increasing monotonically each period)
    luv::CMOResult bull_res{};
    for (int i = 10; i < 25; ++i) {
        bull_res = cmo.update(100.0 + static_cast<double>(i) * 1.0);
    }

    std::cout << "  [Pure Bull] CMO Value: " << bull_res.cmo_value 
              << ", Up Sum: " << bull_res.sum_up 
              << ", Down Sum: " << bull_res.sum_down 
              << ", Spread Multiplier: " << bull_res.dynamic_spread_multiplier << std::endl;

    assert(bull_res.is_valid);
    assert(bull_res.sum_down == 0.0);
    assert(std::abs(bull_res.cmo_value - 100.0) < 1e-4); // Perfect +100
    assert(bull_res.overbought);
    assert(!bull_res.oversold);
    assert(bull_res.dynamic_spread_multiplier > 1.4);

    // 3. Pure Bearish Trend (prices decreasing monotonically)
    cmo.reset();
    for (int i = 0; i < 10; ++i) {
        cmo.update(200.0 - i * 0.1);
    }
    luv::CMOResult bear_res{};
    for (int i = 10; i < 25; ++i) {
        bear_res = cmo.update(200.0 - static_cast<double>(i) * 1.0);
    }

    std::cout << "  [Pure Bear] CMO Value: " << bear_res.cmo_value 
              << ", Up Sum: " << bear_res.sum_up 
              << ", Down Sum: " << bear_res.sum_down << std::endl;

    assert(bear_res.is_valid);
    assert(bear_res.sum_up == 0.0);
    assert(std::abs(bear_res.cmo_value - (-100.0)) < 1e-4); // Perfect -100
    assert(bear_res.oversold);
    assert(!bear_res.overbought);

    std::cout << "[PASS] Chande Momentum Oscillator (CMO) Tests Passed!" << std::endl;
    return 0;
}
