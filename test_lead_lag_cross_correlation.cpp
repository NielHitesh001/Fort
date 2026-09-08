#include "luv_lead_lag_cross_correlation.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Lead-Lag Cross-Correlation Engine Tests..." << std::endl;

    luv::LeadLagCrossCorrelationEngine engine;

    // Case 1: Series X leads Series Y by exactly 2 steps
    // Y[t] = 0.8 * X[t-2] + noise
    // Generate 100 points
    uint32_t lcg = 1337;
    std::array<double, 120> signal_x{};
    for (size_t i = 0; i < 120; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        signal_x[i] = (static_cast<double>(lcg % 1000) - 500.0) / 500.0;
    }

    for (size_t t = 2; t < 102; ++t) {
        double ret_x = signal_x[t];
        double ret_y = 0.9 * signal_x[t - 2]; // Y lags X by 2 steps -> X leads Y by 2 steps
        assert(engine.add_synchronized_point(ret_x, ret_y));
    }

    auto res = engine.compute_lead_lag();
    std::cout << "  Optimal Lag Steps:        " << res.optimal_lag_steps << std::endl;
    std::cout << "  Max Correlation:          " << res.max_correlation << std::endl;
    std::cout << "  Synchronous Correlation:  " << res.synchronous_correlation << std::endl;
    std::cout << "  Significant Lead-Lag:     " << (res.significant_lead_lag ? "YES" : "NO") << std::endl;

    // In our formulation: cross_sum += (x[i] - mean_x) * (y[i + lag] - mean_y)
    // Since y[i+2] = x[i], when lag = +2, x[i] matches y[i+2]
    assert(res.optimal_lag_steps == 2);
    assert(res.max_correlation > 0.80);
    assert(res.significant_lead_lag);

    std::cout << "[PASS] Lead-Lag Cross-Correlation Engine Tests Passed!" << std::endl;
    return 0;
}
