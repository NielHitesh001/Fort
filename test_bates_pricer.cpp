#include "luv_bates_pricer.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace luv;

void test_bates_pricing_and_put_call_parity() {
    BatesParameters p{};
    p.spot_price = 100.0;
    p.strike_price = 100.0;
    p.risk_free_rate = 0.05;
    p.dividend_yield = 0.01;
    p.time_to_maturity = 1.0;
    p.initial_variance = 0.04;        // 20% vol
    p.mean_reversion_kappa = 2.0;
    p.long_term_var_theta = 0.04;
    p.vol_of_vol_xi = 0.25;
    p.correlation_rho = -0.60;
    p.jump_intensity_lambda = 0.20;   // 0.20 jumps / yr
    p.jump_mean_gamma = -0.10;        // -10% mean jump
    p.jump_vol_delta = 0.15;          // 15% jump vol

    auto res = BatesOptionPricer::price_european_option(p);
    assert(res.valid);
    assert(res.call_price > 0.0);
    assert(res.put_price > 0.0);

    // Verify Put-Call Parity: C - P = S0 * e^{-q T} - K * e^{-r T}
    double expected_diff = p.spot_price * std::exp(-p.dividend_yield * p.time_to_maturity) -
                           p.strike_price * std::exp(-p.risk_free_rate * p.time_to_maturity);
    double actual_diff = res.call_price - res.put_price;
    assert(std::fabs(actual_diff - expected_diff) < 1e-3);

    // Verify Greeks
    assert(res.greeks.delta > 0.4 && res.greeks.delta < 0.7); // ATM call delta ~ 0.5 - 0.6
    assert(res.greeks.gamma > 0.0);
    assert(res.greeks.vega > 0.0);
    assert(res.greeks.rho > 0.0);
}

void test_zero_jump_limit_vs_positive_jump() {
    BatesParameters p_no_jump{};
    p_no_jump.spot_price = 100.0;
    p_no_jump.strike_price = 100.0;
    p_no_jump.risk_free_rate = 0.03;
    p_no_jump.dividend_yield = 0.0;
    p_no_jump.time_to_maturity = 0.5;
    p_no_jump.initial_variance = 0.04;
    p_no_jump.mean_reversion_kappa = 1.5;
    p_no_jump.long_term_var_theta = 0.04;
    p_no_jump.vol_of_vol_xi = 0.20;
    p_no_jump.correlation_rho = -0.50;
    p_no_jump.jump_intensity_lambda = 0.0; // No jumps

    auto res_no_jump = BatesOptionPricer::price_european_option(p_no_jump);
    assert(res_no_jump.valid);

    BatesParameters p_with_jump = p_no_jump;
    p_with_jump.jump_intensity_lambda = 0.50;
    p_with_jump.jump_mean_gamma = 0.05;
    p_with_jump.jump_vol_delta = 0.20;

    auto res_with_jump = BatesOptionPricer::price_european_option(p_with_jump);
    assert(res_with_jump.valid);
    // Added jump volatility typically increases option value for ATM options
    assert(res_with_jump.call_price > res_no_jump.call_price);
}

void test_invalid_parameters() {
    BatesParameters p{};
    p.spot_price = -10.0;
    auto res = BatesOptionPricer::price_european_option(p);
    assert(!res.valid);

    p.spot_price = 100.0;
    p.time_to_maturity = 0.0;
    res = BatesOptionPricer::price_european_option(p);
    assert(!res.valid);
}

int main() {
    test_bates_pricing_and_put_call_parity();
    test_zero_jump_limit_vs_positive_jump();
    test_invalid_parameters();
    std::cout << "Bates (1996) Stochastic Volatility Jump-Diffusion Pricer tests passed.\n";
    return 0;
}
