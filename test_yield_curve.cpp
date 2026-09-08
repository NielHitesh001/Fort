#include "luv_yield_curve.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_yield_curve_interpolation_and_discounting() {
    luv::fixed_income::YieldCurveEngine curve;

    // Add US Treasury / SOFR zero curve pillars
    // 3M: 5.20%, 6M: 5.10%, 1Y: 4.80%, 2Y: 4.40%, 5Y: 4.10%, 10Y: 4.25%
    assert(curve.add_pillar(0.25, 0.0520));
    assert(curve.add_pillar(0.50, 0.0510));
    assert(curve.add_pillar(1.00, 0.0480));
    assert(curve.add_pillar(2.00, 0.0440));
    assert(curve.add_pillar(5.00, 0.0410));
    assert(curve.add_pillar(10.00, 0.0425));

    // Exact pillar test
    double r_1y = curve.get_zero_rate(1.0);
    assert(std::fabs(r_1y - 0.0480) < 1e-6);

    // Interpolated 1.5Y tenor between 1Y (4.8%) and 2Y (4.4%) -> 4.6%
    double r_1_5y = curve.get_zero_rate(1.5);
    assert(std::fabs(r_1_5y - 0.0460) < 1e-6);

    // Discount factor at t=1.0: DF = exp(-0.0480 * 1.0) = ~0.953134
    double df_1y = curve.get_discount_factor(1.0);
    assert(std::fabs(df_1y - std::exp(-0.0480)) < 1e-6);

    // Forward rate 1Y to 2Y: f(1, 2) = (0.0440 * 2 - 0.0480 * 1) / 1.0 = 0.0400 (4.00%)
    double fwd_1_2 = curve.get_forward_rate(1.0, 2.0);
    assert(std::fabs(fwd_1_2 - 0.0400) < 1e-6);

    std::printf("[PASS] test_yield_curve_interpolation_and_discounting (1.5Y rate: %.4f, 1Y DF: %.6f, 1Y-2Y Fwd: %.4f)\n",
        r_1_5y, df_1y, fwd_1_2);
}

int main() {
    test_yield_curve_interpolation_and_discounting();
    std::printf("All yield curve engine tests passed successfully.\n");
    return 0;
}
