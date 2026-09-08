#include "luv_cds.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_credit_default_swap_valuation() {
    luv::fixed_income::YieldCurveEngine curve;
    // Flat 4.0% SOFR discounting curve
    assert(curve.add_pillar(0.5, 0.040));
    assert(curve.add_pillar(1.0, 0.040));
    assert(curve.add_pillar(2.0, 0.040));
    assert(curve.add_pillar(5.0, 0.040));

    luv::fixed_income::CreditDefaultSwapEngine engine;

    // $10M Notional, 5-Year CDS, 100 bps running coupon, 100 bps market spread, 40% recovery
    luv::fixed_income::CdsContractDefinition cds{
        .notional = 10'000'000'0000LL,
        .running_spread_bps = 100.0,
        .market_spread_bps = 100.0,
        .recovery_rate = 0.40,
        .tenor_years = 5.0,
        .payment_frequency = 0.25
    };

    auto val = engine.value_cds(cds, curve);

    // Hazard rate lambda = 0.0100 / (1 - 0.40) = 0.01667 (1.67% default intensity)
    assert(std::fabs(val.hazard_rate - (0.0100 / 0.60)) < 1e-4);
    // 5-year survival probability Q(5) = exp(-0.01667 * 5) = ~0.920
    assert(val.survival_probability_5y > 0.90 && val.survival_probability_5y < 0.95);

    // Since running coupon == market spread, upfront payment in dollar terms should be near 0
    assert(std::fabs(val.upfront_payment / 10000.0) < 50000.0); // very small residual (< $50k on $10M)

    std::printf("[PASS] test_credit_default_swap_valuation (Hazard Rate: %.4f, 5Y Survival: %.2f%%, Upfront: $%.2f)\n",
        val.hazard_rate, val.survival_probability_5y * 100.0, val.upfront_payment / 10000.0);
}

int main() {
    test_credit_default_swap_valuation();
    std::printf("All CDS valuation tests passed successfully.\n");
    return 0;
}
