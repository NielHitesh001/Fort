#include "luv_cartea_jaimungal.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Cartea-Jaimungal (2014) Alpha-Drift MM Tests..." << std::endl;

    luv::CarteaJaimungalParams params{};
    params.risk_aversion_gamma = 0.08;
    params.order_flow_kappa = 1.5;
    params.volatility_sigma = 0.02;
    params.alpha_sensitivity = 1.0;
    params.max_inventory = 50;
    params.min_tick_size = 0.01;

    luv::CarteaJaimungalEngine engine(params);

    double mid = 100.00;

    // 1. Neutral Drift (alpha = 0, q = 0)
    auto q_neutral = engine.calculate_quotes(mid, 0, 0.0);
    std::cout << "  [Neutral alpha=0] Bid: " << q_neutral.optimal_bid_price 
              << ", Ask: " << q_neutral.optimal_ask_price << std::endl;

    assert(q_neutral.optimal_bid_price < mid);
    assert(q_neutral.optimal_ask_price > mid);
    assert(std::abs(q_neutral.quote_center_shift) < 1e-6);

    // 2. Bullish Alpha Drift (alpha = +0.15): Quotes should shift UPWARDS to capture momentum
    auto q_bull = engine.calculate_quotes(mid, 0, +0.15);
    std::cout << "  [Bullish alpha=+0.15] Bid: " << q_bull.optimal_bid_price 
              << ", Ask: " << q_bull.optimal_ask_price 
              << ", Shift: +" << q_bull.quote_center_shift << std::endl;

    assert(q_bull.quote_center_shift > 0.0);
    assert(q_bull.optimal_bid_price >= q_neutral.optimal_bid_price);
    assert(q_bull.optimal_ask_price >= q_neutral.optimal_ask_price);

    // 3. Bearish Alpha Drift (alpha = -0.15): Quotes should shift DOWNWARDS
    auto q_bear = engine.calculate_quotes(mid, 0, -0.15);
    std::cout << "  [Bearish alpha=-0.15] Bid: " << q_bear.optimal_bid_price 
              << ", Ask: " << q_bear.optimal_ask_price 
              << ", Shift: " << q_bear.quote_center_shift << std::endl;

    assert(q_bear.quote_center_shift < 0.0);
    assert(q_bear.optimal_bid_price <= q_neutral.optimal_bid_price);
    assert(q_bear.optimal_ask_price <= q_neutral.optimal_ask_price);

    // 4. Combined Inventory & Alpha: Long Inventory (q = +20) with Bullish Alpha (alpha = +0.10)
    auto q_combo = engine.calculate_quotes(mid, +20, +0.10);
    std::cout << "  [Combo q=+20, alpha=+0.10] Bid: " << q_combo.optimal_bid_price 
              << ", Ask: " << q_combo.optimal_ask_price << std::endl;

    assert(q_combo.optimal_bid_price > 0.0);
    assert(q_combo.optimal_ask_price > 0.0);

    std::cout << "[PASS] Cartea-Jaimungal (2014) Alpha-Drift MM Tests Passed!" << std::endl;
    return 0;
}
