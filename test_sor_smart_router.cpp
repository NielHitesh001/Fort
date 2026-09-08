#include <iostream>
#include <cassert>
#include "luv_sor_smart_router.hpp"

int main() {
    std::cout << "[TEST] Running Smart Order Router Dark/Lit Splitting Test...\n";

    // Setup 4 venues for Buy order:
    // Order: Buy 1,000 shares limit $100.10 (1,001,000)
    // 1. Dark Midpoint (Venue 1): Midpoint Ask $100.05 (1,000,500), 300 shares available
    // 2. Lit Venue A (Venue 2): Ask $100.10, 400 shares, Fee -3 bps, Fill Prob 90%
    // 3. Lit Venue B (Venue 3): Ask $100.10, 500 shares, Maker Rebate +2 bps, Fill Prob 95%
    // 4. Lit Venue C (Venue 4): Ask $100.08 (1,000,800), 200 shares, Fee -1 bp
    
    luv::VenueQuote venues[4];
    venues[0].venue_id = 1; // Dark
    venues[0].is_dark = true;
    venues[0].ask_price = 1'000'500; // $100.05
    venues[0].ask_size = 300;

    venues[1].venue_id = 2; // Lit A
    venues[1].is_dark = false;
    venues[1].ask_price = 1'001'000; // $100.10
    venues[1].ask_size = 400;
    venues[1].fee_or_rebate_bps = -3;
    venues[1].fill_probability_pct = 90;

    venues[2].venue_id = 3; // Lit B (Better rebate than A)
    venues[2].is_dark = false;
    venues[2].ask_price = 1'001'000; // $100.10
    venues[2].ask_size = 500;
    venues[2].fee_or_rebate_bps = 2;
    venues[2].fill_probability_pct = 95;

    venues[3].venue_id = 4; // Lit C (Better price $100.08)
    venues[3].is_dark = false;
    venues[3].ask_price = 1'000'800; // $100.08
    venues[3].ask_size = 200;
    venues[3].fee_or_rebate_bps = -1;
    venues[3].fill_probability_pct = 99;

    auto plan = luv::SmartOrderRouter::route_order(true, 1'001'000, 1000, venues, 4);

    assert(plan.total_routed_qty == 1000);
    assert(plan.remaining_unfilled_qty == 0);
    assert(plan.slice_count == 3);

    // 1st slice: Dark Midpoint (300 @ $100.05)
    assert(plan.slices[0].venue_id == 1);
    assert(plan.slices[0].is_dark);
    assert(plan.slices[0].route_quantity == 300);
    assert(plan.slices[0].target_price == 1'000'500);

    // 2nd slice: Lit C (Best price 200 @ $100.08)
    assert(plan.slices[1].venue_id == 4);
    assert(!plan.slices[1].is_dark);
    assert(plan.slices[1].route_quantity == 200);
    assert(plan.slices[1].target_price == 1'000'800);

    // 3rd slice: Lit B (Remaining 500 @ $100.10 with +2 bps rebate preferred over Lit A)
    assert(plan.slices[2].venue_id == 3);
    assert(!plan.slices[2].is_dark);
    assert(plan.slices[2].route_quantity == 500);
    assert(plan.slices[2].target_price == 1'001'000);

    std::cout << "[TEST] Routed " << plan.total_routed_qty << " shares across " << plan.slice_count << " slices.\n";
    std::cout << "[TEST] Smart Order Router Dark/Lit Splitting Test Passed!\n";
    return 0;
}
