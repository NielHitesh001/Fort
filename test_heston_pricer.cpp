#include "luv_heston_pricer.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Heston (1993) Stochastic Volatility Option Pricer Tests..." << std::endl;

    // 1. Standard ATM European Call Option
    // S0 = 100, K = 100, r = 3%, T = 1 year, v0 = 0.04 (sigma = 20%), kappa = 2.0, theta = 0.04, xi = 0.3, rho = -0.7
    luv::HestonParameters p1{};
    p1.spot_price = 100.0;
    p1.strike_price = 100.0;
    p1.risk_free_rate = 0.03;
    p1.time_to_maturity = 1.0;
    p1.initial_variance = 0.04;
    p1.mean_reversion_kappa = 2.0;
    p1.long_term_var_theta = 0.04;
    p1.vol_of_vol_xi = 0.30;
    p1.correlation_rho = -0.70;

    auto res1 = luv::HestonOptionPricer::price_european_option(p1);
    std::cout << "  ATM Heston Call Price: $" << res1.call_price << std::endl;
    std::cout << "  ATM Heston Put Price:  $" << res1.put_price << std::endl;

    assert(res1.valid);
    // Theoretical Black-Scholes ATM with 20% vol is ~9.41; Heston with rho = -0.7 is ~8.5-9.5
    assert(res1.call_price > 7.0 && res1.call_price < 11.0);
    // Put-Call parity check: C - P = S0 - K * exp(-r*T) = 100 - 100 * exp(-0.03) = ~2.955
    double parity_diff = res1.call_price - res1.put_price;
    double expected_parity = p1.spot_price - p1.strike_price * std::exp(-p1.risk_free_rate * p1.time_to_maturity);
    std::cout << "  Parity Diff (C - P):   $" << parity_diff << ", Expected: $" << expected_parity << std::endl;
    assert(std::abs(parity_diff - expected_parity) < 0.05);

    // 2. Deep ITM Option (K = 70)
    luv::HestonParameters p2 = p1;
    p2.strike_price = 70.0;
    auto res2 = luv::HestonOptionPricer::price_european_option(p2);
    std::cout << "  Deep ITM Call Price:   $" << res2.call_price << std::endl;
    assert(res2.call_price > (p2.spot_price - p2.strike_price)); // Strictly above intrinsic

    // 3. Deep OTM Option (K = 140)
    luv::HestonParameters p3 = p1;
    p3.strike_price = 140.0;
    auto res3 = luv::HestonOptionPricer::price_european_option(p3);
    std::cout << "  Deep OTM Call Price:   $" << res3.call_price << std::endl;
    assert(res3.call_price > 0.0 && res3.call_price < 2.0);

    std::cout << "[PASS] Heston (1993) Stochastic Volatility Option Pricer Tests Passed!" << std::endl;
    return 0;
}
