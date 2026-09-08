#include "luv_extreme_value_volatility.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Extreme Value Realized Volatility Engine Tests..." << std::endl;

    luv::ExtremeValueVolatilityEngine engine;

    // Simulate 50 OHLC bars of an asset around $100 with known daily intra-bar range
    // High/Low spread ~1.5%, Open/Close range ~0.5%
    for (size_t i = 0; i < 50; ++i) {
        luv::OHLCBar bar{};
        bar.timestamp_ns = (i + 1) * 60'000'000'000ULL;
        double base = 100.0 + std::sin(static_cast<double>(i) * 0.2);
        bar.open = base;
        bar.high = base + 0.75;
        bar.low = base - 0.75;
        bar.close = base + ((i % 2 == 0) ? 0.25 : -0.25);
        bar.volume = 10000;
        assert(engine.add_bar(bar));
    }

    assert(engine.bar_count() == 50);
    auto vol_res = engine.calculate_volatilities();

    std::cout << "  Parkinson Volatility:      " << vol_res.parkinson_vol << std::endl;
    std::cout << "  Garman-Klass Volatility:   " << vol_res.garman_klass_vol << std::endl;
    std::cout << "  Rogers-Satchell Vol:       " << vol_res.rogers_satchell_vol << std::endl;
    std::cout << "  Yang-Zhang Vol:            " << vol_res.yang_zhang_vol << std::endl;
    std::cout << "  Close-to-Close Vol:        " << vol_res.close_to_close_vol << std::endl;

    assert(vol_res.parkinson_vol > 0.0);
    assert(vol_res.garman_klass_vol > 0.0);
    assert(vol_res.rogers_satchell_vol > 0.0);
    assert(vol_res.yang_zhang_vol > 0.0);

    // Extreme value estimators are strictly positive and capture the intra-bar range
    assert(vol_res.parkinson_vol > 0.005 && vol_res.parkinson_vol < 0.05);
    assert(vol_res.garman_klass_vol > 0.005 && vol_res.garman_klass_vol < 0.05);

    std::cout << "[PASS] Extreme Value Realized Volatility Engine Tests Passed!" << std::endl;
    return 0;
}
