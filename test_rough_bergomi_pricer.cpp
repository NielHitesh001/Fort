#include "luv_rough_bergomi_pricer.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace luv;

void test_rough_bergomi_pricing_and_skew() {
    RoughBergomiParameters p{};
    p.spot_price = 100.0;
    p.strike_price = 100.0;
    p.risk_free_rate = 0.03;
    p.time_to_maturity = 0.10;      // 0.1 years (short-dated)
    p.initial_forward_var = 0.04;   // 20% vol
    p.hurst_parameter = 0.10;       // Rough H = 0.1
    p.vol_of_vol_eta = 1.80;
    p.correlation_rho = -0.70;      // Negative skew

    auto res = RoughBergomiPricer::price_option(p);
    assert(res.valid);
    assert(res.call_price > 0.0);
    assert(res.put_price > 0.0);

    // ATM Skew is negative due to rho < 0
    assert(res.atm_skew < 0.0);

    // Power law explosion: shorter maturity T -> higher absolute skew |Skew|
    RoughBergomiParameters p_shorter = p;
    p_shorter.time_to_maturity = 0.02; // Very short maturity
    auto res_shorter = RoughBergomiPricer::price_option(p_shorter);
    assert(std::fabs(res_shorter.atm_skew) > std::fabs(res.atm_skew));

    // Put-Call Parity: C - P = S0 - K * e^{-r T}
    double expected_diff = p.spot_price - p.strike_price * std::exp(-p.risk_free_rate * p.time_to_maturity);
    double actual_diff = res.call_price - res.put_price;
    assert(std::fabs(actual_diff - expected_diff) < 1e-4);
}

void test_invalid_hurst_parameters() {
    RoughBergomiParameters p{};
    p.hurst_parameter = 0.60; // H >= 0.5 is not rough
    auto res = RoughBergomiPricer::price_option(p);
    assert(!res.valid);

    p.hurst_parameter = -0.10;
    res = RoughBergomiPricer::price_option(p);
    assert(!res.valid);
}

int main() {
    test_rough_bergomi_pricing_and_skew();
    test_invalid_hurst_parameters();
    std::cout << "Rough Bergomi Fractional Stochastic Volatility Pricer tests passed.\n";
    return 0;
}
