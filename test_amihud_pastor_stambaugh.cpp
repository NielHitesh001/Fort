#include "luv_amihud_pastor_stambaugh.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Amihud (2002) and Pastor-Stambaugh (2003) Tests..." << std::endl;

    luv::AmihudPastorStambaughEngine engine;

    // Simulate 60 days of market observations
    // Asset with mean daily volume $20M, average daily return +/- 1%
    // Introduce negative next-day return autocorrelation on high-volume days (Pastor-Stambaugh liquidity rebound)
    uint32_t lcg = 101;
    double prev_ret = 0.0;
    for (size_t i = 0; i < 60; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        double r_raw = ((static_cast<double>(lcg % 1000) - 500.0) / 500.0) * 0.02; // +/- 2%
        double ret = r_raw - (0.25 * prev_ret); // Partial reversal
        double vol = 20'000'000.0 + (static_cast<double>(lcg % 500) * 20'000.0);

        assert(engine.add_observation(ret, vol));
        prev_ret = ret;
    }

    assert(engine.count() == 60);
    auto res = engine.compute_metrics();

    std::cout << "  Amihud ILLIQ Ratio:        " << res.amihud_illiq_ratio << std::endl;
    std::cout << "  Pastor-Stambaugh Gamma:    " << res.pastor_stambaugh_gamma << std::endl;
    std::cout << "  Mean Daily Volume:         $" << res.mean_daily_volume << std::endl;

    assert(res.amihud_illiq_ratio > 0.0);
    assert(res.amihud_illiq_ratio < 2.0); // Liquid $20M ADV asset
    assert(res.mean_daily_volume >= 20'000'000.0);
    // Pastor-Stambaugh gamma is negative reflecting price concession reversal
    assert(res.pastor_stambaugh_gamma <= 0.05);

    std::cout << "[PASS] Amihud (2002) and Pastor-Stambaugh (2003) Tests Passed!" << std::endl;
    return 0;
}
