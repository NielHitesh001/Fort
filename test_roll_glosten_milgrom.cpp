#include "luv_roll_glosten_milgrom.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Roll (1984) and Glosten-Milgrom (1985) Tests..." << std::endl;

    // 1. Roll (1984) Spread Estimator Tests
    luv::RollSpreadEstimator roll;
    // Simulate independent Bernoulli order flow: P_t = M_t + c * Q_t with c = 0.05 (True Spread S = 0.10)
    // Deterministic pseudo-random sequence with zero mean direction
    double mid = 100.0;
    double c = 0.05;
    // 200 trade steps with known signs
    uint32_t lcg = 42;
    for (int i = 0; i < 300; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        int q = ((lcg >> 16) & 1) ? 1 : -1;
        double p = mid + c * q;
        roll.add_price(p);
    }

    auto roll_res = roll.compute_roll_spread();
    std::cout << "  Roll Autocovariance: " << roll_res.autocovariance << std::endl;
    std::cout << "  Roll Effective Spread: $" << roll_res.effective_spread << std::endl;

    assert(roll_res.autocovariance < 0.0);
    // Theoretical spread is 2c = 0.10; effective spread should match within statistical bounds
    assert(std::abs(roll_res.effective_spread - 0.10) < 0.03);

    // 2. Glosten-Milgrom (1985) Sequential Trade Model Tests
    luv::GlostenMilgromParams gm_params{};
    gm_params.v_high = 110.0;
    gm_params.v_low = 90.0;
    gm_params.alpha_informed = 0.30; // 30% informed traders
    gm_params.initial_p_high = 0.50;

    luv::GlostenMilgromEngine gm(gm_params);

    // At prior p = 0.50, initial symmetric quotes around mid 100.0
    auto q0 = gm.get_quotes();
    std::cout << "  [Initial Prior] Mid: " << q0.mid_price 
              << ", Ask: " << q0.ask_price 
              << ", Bid: " << q0.bid_price 
              << ", Adverse Selection Spread: $" << q0.adverse_selection_spread << std::endl;

    assert(std::abs(q0.mid_price - 100.0) < 1e-4);
    assert(q0.ask_price > 100.0);
    assert(q0.bid_price < 100.0);
    assert(q0.adverse_selection_spread > 0.0);

    // Observe consecutive BUY orders (informed buying flow)
    for (int i = 0; i < 10; ++i) {
        gm.process_trade(true); // Buy observed
    }

    auto q_after_buys = gm.get_quotes();
    std::cout << "  [After 10 Buys] p_high: " << q_after_buys.current_p_high 
              << ", Mid: " << q_after_buys.mid_price << std::endl;

    assert(q_after_buys.current_p_high > 0.90);
    assert(q_after_buys.mid_price > 105.0); // Drifted towards V_H (110)

    // Observe consecutive SELL orders
    for (int i = 0; i < 20; ++i) {
        gm.process_trade(false); // Sell observed
    }

    auto q_after_sells = gm.get_quotes();
    std::cout << "  [After 20 Sells] p_high: " << q_after_sells.current_p_high 
              << ", Mid: " << q_after_sells.mid_price << std::endl;

    assert(q_after_sells.current_p_high < 0.20);
    assert(q_after_sells.mid_price < 95.0); // Drifted towards V_L (90)

    std::cout << "[PASS] Roll (1984) and Glosten-Milgrom (1985) Tests Passed!" << std::endl;
    return 0;
}
