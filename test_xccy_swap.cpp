#include "luv_xccy_swap.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_cross_currency_basis_swap() {
    luv::fixed_income::CrossCurrencySwapEngine engine;

    // EUR/USD 1-Year Quote:
    // Spot: 1.0850, EUR Rate: 3.50%, USD Rate: 5.00%, Basis: -25 bps (-0.25%) on EUR
    luv::fixed_income::CrossCurrencyQuote q{
        .base_ccy_id = 1,
        .quote_ccy_id = 2,
        .spot_fx_rate = 1.0850,
        .base_zero_rate = 0.0350,
        .quote_zero_rate = 0.0500,
        .basis_spread_bps = -25.0,
        .tenor_years = 1.0
    };

    auto res = engine.evaluate_xccy_swap(q);

    assert(res.theoretical_forward_fx > q.spot_fx_rate);
    assert(res.basis_adjusted_forward_fx > res.theoretical_forward_fx);
    assert(res.cip_deviation_bps > 20.0); // ~25 bps deviation
    assert(res.arbitrage_opportunity == true);

    std::printf("[PASS] test_cross_currency_basis_swap (Theo Fwd: %.4f, Basis Adj: %.4f, CIP Dev: %.2f bps)\n",
        res.theoretical_forward_fx, res.basis_adjusted_forward_fx, res.cip_deviation_bps);
}

int main() {
    test_cross_currency_basis_swap();
    std::printf("All cross-currency swap tests passed successfully.\n");
    return 0;
}
