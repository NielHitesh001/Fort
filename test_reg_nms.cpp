#include "luv_reg_nms.hpp"
#include <cassert>
#include <cstdio>

void test_nbbo_computation() {
    luv::reg_nms::RegNmsRule611Validator validator;

    // Venue 1 (Nasdaq): Bid 10000 (sz 100), Ask 10020 (sz 200)
    validator.update_venue_bbo(1, 10000, 10020, 100, 200, 100);
    // Venue 2 (NYSE): Bid 10010 (sz 300), Ask 10030 (sz 150) -> Higher Bid!
    validator.update_venue_bbo(2, 10010, 10030, 300, 150, 110);
    // Venue 3 (BATS): Bid 10000 (sz 50), Ask 10015 (sz 400) -> Lower Ask!
    validator.update_venue_bbo(3, 10000, 10015, 50, 400, 120);

    auto nbbo = validator.compute_nbbo();
    assert(nbbo.national_best_bid == 10010);
    assert(nbbo.best_bid_venue == 2); // NYSE
    assert(nbbo.national_best_ask == 10015);
    assert(nbbo.best_ask_venue == 3); // BATS
    assert(nbbo.total_bid_size == 300);
    assert(nbbo.total_ask_size == 400);

    std::printf("[PASS] test_nbbo_computation\n");
}

void test_trade_through_prevention() {
    luv::reg_nms::RegNmsRule611Validator validator;
    // NBBO: 10010 / 10015
    validator.update_venue_bbo(1, 10010, 10015, 100, 100, 100);

    // 1. Valid executions at or within NBBO
    assert(validator.validate_execution(luv::exec::kBuy, 10015, false));
    assert(validator.validate_execution(luv::exec::kSell, 10010, false));

    // 2. Unlawful Trade-Through: Buy at 10020 (worse than National Best Ask 10015)
    assert(!validator.validate_execution(luv::exec::kBuy, 10020, false));

    // 3. Unlawful Trade-Through: Sell at 10005 (worse than National Best Bid 10010)
    assert(!validator.validate_execution(luv::exec::kSell, 10005, false));

    // 4. ISO (Intermarket Sweep Order) exemption: allowed to bypass local Rule 611
    assert(validator.validate_execution(luv::exec::kBuy, 10020, true));
    assert(validator.validate_execution(luv::exec::kSell, 10005, true));

    std::printf("[PASS] test_trade_through_prevention\n");
}

int main() {
    test_nbbo_computation();
    test_trade_through_prevention();
    std::printf("All Reg NMS Rule 611 tests passed successfully.\n");
    return 0;
}
