#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_almgren_chriss.hpp"

int main() {
    std::cout << "[TEST] Running Almgren-Chriss Optimal Liquidation Trajectory Test...\n";

    luv::AlmgrenChrissParams params;
    params.total_shares = 10000.0;
    params.time_horizon_sec = 300.0; // 5 min
    params.volatility_sigma = 0.02;
    params.risk_aversion_lambda = 1e-4;
    params.temp_impact_eta = 2.5e-4;
    params.num_intervals = 5;

    luv::ExecutionStep steps[10];
    size_t count = luv::AlmgrenChrissModel::compute_optimal_trajectory(params, steps, 10);

    assert(count == 6); // 0, 1, 2, 3, 4, 5
    assert(std::abs(steps[0].target_holdings - 10000.0) < 1e-4);
    assert(std::abs(steps[5].target_holdings - 0.0) < 1e-4);

    double total_traded = 0.0;
    for (size_t i = 1; i < count; ++i) {
        assert(steps[i].target_holdings < steps[i - 1].target_holdings); // Monotonically decreasing
        assert(steps[i].trade_shares > 0.0);
        total_traded += steps[i].trade_shares;
    }

    assert(std::abs(total_traded - 10000.0) < 1e-4);

    std::cout << "[TEST] Initial Holdings: " << steps[0].target_holdings
              << " | Final Holdings: " << steps[5].target_holdings
              << " | Total Traded: " << total_traded << "\n";

    std::cout << "[TEST] Almgren-Chriss Optimal Liquidation Trajectory Test Passed!\n";
    return 0;
}
