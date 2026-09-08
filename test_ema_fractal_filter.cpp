#include "luv_ema_fractal_filter.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Multi-Horizon EMA Ribbon & Fractal Dimension Filter Tests..." << std::endl;

    luv::EMAFractalFilterEngine engine;

    // 1. Simulate Strong Bullish Trend: monotonically increasing prices 100 -> 150
    luv::EMARibbonState state_trend{};
    for (int i = 0; i < 50; ++i) {
        double p = 100.0 + static_cast<double>(i) * 1.0;
        state_trend = engine.update(p);
    }

    std::cout << "  [Strong Bull Trend] Alignment Score: " << state_trend.alignment_score 
              << ", Ribbon Spread: " << state_trend.ribbon_spread_pct << "%"
              << ", Fractal Dim: " << state_trend.fractal_dimension 
              << ", Is Trending: " << (state_trend.is_trending ? "YES" : "NO") << std::endl;

    assert(state_trend.alignment_score == 5); // Perfect bull alignment: EMA5 > EMA8 > ... > EMA55
    assert(state_trend.ribbon_spread_pct > 0.0);
    assert(state_trend.fractal_dimension < 1.35); // Clean low fractal dimension
    assert(state_trend.is_trending);
    assert(!state_trend.is_ranging);

    // 2. Simulate Highly Noisy / Ranging Market (high fractal dimension Brownian oscillation)
    engine.reset();
    luv::EMARibbonState state_range{};
    uint32_t lcg = 777;
    for (int i = 0; i < 50; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        double noise = ((static_cast<double>(lcg % 1000) - 500.0) / 500.0) * 5.0; // +/- $5 noise around 100
        double p = 100.0 + noise;
        state_range = engine.update(p);
    }

    std::cout << "  [Noisy Range] Alignment Score: " << state_range.alignment_score 
              << ", Fractal Dim: " << state_range.fractal_dimension 
              << ", Is Ranging: " << (state_range.is_ranging ? "YES" : "NO") << std::endl;

    assert(state_range.fractal_dimension >= 1.45); // High fractal dimension
    assert(state_range.is_ranging);

    std::cout << "[PASS] Multi-Horizon EMA Ribbon & Fractal Dimension Filter Tests Passed!" << std::endl;
    return 0;
}
