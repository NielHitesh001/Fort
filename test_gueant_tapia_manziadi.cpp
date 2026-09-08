#include "luv_gueant_tapia_manziadi.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Guéant-Tapia-Manziadi (GTM) MM Tests..." << std::endl;

    luv::GTMParameters params{};
    params.risk_aversion_gamma = 0.05;
    params.order_flow_kappa = 1.5;
    params.volatility_sigma = 0.02;
    params.intensity_A = 100.0;
    params.max_inventory = 50;
    params.min_tick_size = 0.01;

    luv::GueantTapiaManziadiEngine engine(params);

    // 1. Flat Inventory (q = 0)
    double mid = 100.00;
    auto q0 = engine.calculate_quotes(mid, 0);

    std::cout << "  [q = 0] Bid: " << q0.optimal_bid_price 
              << ", Ask: " << q0.optimal_ask_price 
              << ", Half Spread: " << q0.expected_half_spread << std::endl;

    assert(q0.optimal_bid_price < mid);
    assert(q0.optimal_ask_price > mid);
    // At q = 0, delta_bid and delta_ask are approximately symmetric
    assert(std::abs(q0.delta_ask - q0.delta_bid) < 0.005);
    assert(std::abs(q0.reservation_price - mid) < 1e-6);

    // 2. Long Inventory (q = +20): Should lower quotes (skew downward) to attract buyers and disincentivize sellers
    auto q_long = engine.calculate_quotes(mid, +20);
    std::cout << "  [q = +20] Bid: " << q_long.optimal_bid_price 
              << ", Ask: " << q_long.optimal_ask_price 
              << ", Reservation: " << q_long.reservation_price << std::endl;

    assert(q_long.reservation_price < mid);
    assert(q_long.delta_bid > q0.delta_bid); // Bid placed further away / deeper
    assert(q_long.delta_ask < q0.delta_ask); // Ask placed closer to mid to dump inventory
    assert(q_long.optimal_ask_price <= q0.optimal_ask_price);

    // 3. Short Inventory (q = -20): Should raise quotes (skew upward)
    auto q_short = engine.calculate_quotes(mid, -20);
    std::cout << "  [q = -20] Bid: " << q_short.optimal_bid_price 
              << ", Ask: " << q_short.optimal_ask_price 
              << ", Reservation: " << q_short.reservation_price << std::endl;

    assert(q_short.reservation_price > mid);
    assert(q_short.delta_ask > q0.delta_ask); // Ask placed further away
    assert(q_short.delta_bid < q0.delta_bid); // Bid placed closer to buy back
    assert(q_short.optimal_bid_price >= q0.optimal_bid_price);

    // 4. Hard Inventory Boundary (q = +50 max limit)
    auto q_max = engine.calculate_quotes(mid, +50);
    assert(q_max.optimal_bid_price == 0.0); // Bidding suppressed completely
    assert(q_max.optimal_ask_price > 0.0);  // Ask remains active

    std::cout << "[PASS] Guéant-Tapia-Manziadi (GTM) MM Tests Passed!" << std::endl;
    return 0;
}
