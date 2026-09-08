#include "luv_greeks.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_black_scholes_call_and_put() {
    double spot = 100.0;
    double strike = 100.0;
    double tte = 1.0;     // 1 year
    double r = 0.05;      // 5% rate
    double vol = 0.20;    // 20% vol

    auto call = luv::greeks::BlackScholesEngine::compute_greeks(
        luv::greeks::OptionType::kCall, spot, strike, tte, r, vol);

    // Theoretical BS ATM Call Price ~ 10.45, Delta ~ 0.637
    assert(std::abs(call.price - 10.45) < 0.20);
    assert(call.delta > 0.50 && call.delta < 0.70);
    assert(call.gamma > 0.0);
    assert(call.vega > 0.0);

    auto put = luv::greeks::BlackScholesEngine::compute_greeks(
        luv::greeks::OptionType::kPut, spot, strike, tte, r, vol);

    // Put-Call Parity: C - P = S - K * exp(-r*T)
    double disc_strike = strike * std::exp(-r * tte);
    double parity_diff = (call.price - put.price) - (spot - disc_strike);
    assert(std::abs(parity_diff) < 1e-4);

    // Call Delta - Put Delta = 1
    assert(std::abs((call.delta - put.delta) - 1.0) < 1e-4);

    std::printf("[PASS] test_black_scholes_call_and_put (Call: %.2f, Put: %.2f)\n", call.price, put.price);
}

void test_portfolio_greeks_limits() {
    luv::greeks::PortfolioRiskLimits limits;
    limits.max_net_delta = 300.0;
    limits.max_gross_gamma = 50.0;

    luv::greeks::OptionsPortfolioRisk portfolio(limits);

    // Long 4 Call contracts (100 shares per contract) ATM -> Delta ~ +250
    portfolio.add_position(luv::greeks::OptionType::kCall, 100.0, 100.0, 1.0, 0.05, 0.20, 4);

    bool delta_breach = false;
    bool gamma_breach = false;
    assert(portfolio.check_risk(delta_breach, gamma_breach));
    assert(!delta_breach);

    // Add another 2 Call contracts -> Net Delta ~ +380 > 300 Limit -> Breach!
    portfolio.add_position(luv::greeks::OptionType::kCall, 100.0, 100.0, 1.0, 0.05, 0.20, 2);
    assert(!portfolio.check_risk(delta_breach, gamma_breach));
    assert(delta_breach);

    std::printf("[PASS] test_portfolio_greeks_limits\n");
}

int main() {
    test_black_scholes_call_and_put();
    test_portfolio_greeks_limits();
    std::printf("All options Greeks & portfolio risk tests passed successfully.\n");
    return 0;
}
