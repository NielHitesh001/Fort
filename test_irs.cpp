#include "luv_irs.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_interest_rate_swap_valuation() {
    luv::fixed_income::YieldCurveEngine curve;
    // Flat 5.0% zero curve
    assert(curve.add_pillar(0.5, 0.050));
    assert(curve.add_pillar(1.0, 0.050));
    assert(curve.add_pillar(2.0, 0.050));
    assert(curve.add_pillar(5.0, 0.050));

    luv::fixed_income::InterestRateSwapEngine engine;

    // $10M Notional, 5-Year swap, 5.0% Fixed Rate (par swap expectation)
    luv::fixed_income::SwapLegDefinition swap{
        .notional = 10'000'000'0000LL, // $10M * 10,000
        .fixed_rate = 0.050,
        .tenor_years = 5.0,
        .payment_frequency_years = 0.5,
        .day_count = luv::fixed_income::DayCountConvention::kAct360
    };

    auto val = engine.value_swap(swap, curve);

    // Par swap rate should be approximately 5.0%
    assert(std::fabs(val.par_swap_rate - 0.050) < 0.005);
    // DV01 should be positive and proportional to notional * 5y duration
    assert(val.dv01 > 0.0);

    std::printf("[PASS] test_interest_rate_swap_valuation (Par Swap Rate: %.3f%%, DV01: $%.2f)\n",
        val.par_swap_rate * 100.0, val.dv01);
}

int main() {
    test_interest_rate_swap_valuation();
    std::printf("All interest rate swap tests passed successfully.\n");
    return 0;
}
