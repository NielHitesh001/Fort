#include "luv_bond_analytics.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_bond_pricing_and_duration() {
    luv::fixed_income::BondAnalyticsEngine engine;

    // 10-Year Treasury Bond, 5.0% coupon, $100 par, semi-annual
    luv::fixed_income::FixedCouponBond bond{
        .coupon_rate = 0.05,
        .face_value = 100.0,
        .maturity_years = 10.0,
        .payment_frequency = 0.5
    };

    // Par yield: YTM = 5.0% -> Price = $100.00
    auto par_analytics = engine.compute_analytics(bond, 0.05);
    assert(std::fabs(par_analytics.clean_price - 100.0) < 1e-4);
    assert(par_analytics.modified_duration > 7.0 && par_analytics.modified_duration < 8.5);
    assert(par_analytics.convexity > 60.0);

    // Discount yield: YTM = 6.0% -> Price < $100.00
    auto disc_analytics = engine.compute_analytics(bond, 0.06);
    assert(disc_analytics.clean_price < 100.0);

    // Premium yield: YTM = 4.0% -> Price > $100.00
    auto prem_analytics = engine.compute_analytics(bond, 0.04);
    assert(prem_analytics.clean_price > 100.0);

    // YTM Solver: Target price $92.56 -> Solve YTM ~ 6.0%
    double solved_ytm = engine.solve_ytm_from_price(bond, disc_analytics.clean_price);
    assert(std::fabs(solved_ytm - 0.06) < 1e-4);

    std::printf("[PASS] test_bond_pricing_and_duration (Par: $%.2f, ModDur: %.2f, Convexity: %.2f, Solved YTM: %.2f%%)\n",
        par_analytics.clean_price, par_analytics.modified_duration, par_analytics.convexity, solved_ytm * 100.0);
}

int main() {
    test_bond_pricing_and_duration();
    std::printf("All bond analytics tests passed successfully.\n");
    return 0;
}
