#include <iostream>
#include <cassert>
#include "luv_10b5_1_plan.hpp"

int main() {
    std::cout << "[TEST] Running SEC Rule 10b5-1 Trading Plan Validator Test...\n";

    uint64_t day = luv::Plan10b51Validator::kDayInNs;
    uint64_t t0 = 1'000 * day;

    // 1. Director 10b5-1 Plan (90-day cooling-off period)
    luv::TradingPlan10b51 plan;
    plan.plan_id = 1;
    plan.insider_id = 1001;
    plan.role = luv::InsiderRole::DirectorOrOfficer;
    plan.plan_adoption_time_ns = t0;
    plan.cooling_off_period_ns = 90 * day;
    plan.is_active = true;

    luv::PlannedTradeOrder order;
    order.symbol_id = 10;
    order.is_buy = false;
    order.min_price = 1'000'000; // $100.00 min limit
    order.max_price = 0;
    order.target_quantity = 5000;

    // Day 45 (within 90-day cooling-off period) -> Blocked
    assert(!luv::Plan10b51Validator::is_trade_permitted(plan, t0 + 45 * day, order, 1'050'000, 1000));

    // Day 95 (>90-day cooling-off period) -> Permitted
    assert(luv::Plan10b51Validator::is_trade_permitted(plan, t0 + 95 * day, order, 1'050'000, 1000));

    // Price below formula limit ($95.00 < $100.00 min) -> Blocked
    assert(!luv::Plan10b51Validator::is_trade_permitted(plan, t0 + 95 * day, order, 950'000, 1000));

    // Quantity exceeding target -> Blocked
    assert(!luv::Plan10b51Validator::is_trade_permitted(plan, t0 + 95 * day, order, 1'050'000, 6000));

    // 2. Single-Trade Plan 12-month restriction
    plan.is_single_trade_plan = true;
    plan.last_single_trade_plan_ns = t0 - 200 * day; // 200 days ago (< 365 days) -> Blocked
    assert(!luv::Plan10b51Validator::is_trade_permitted(plan, t0 + 95 * day, order, 1'050'000, 1000));

    // 400 days ago (> 365 days) -> Permitted
    plan.last_single_trade_plan_ns = t0 - 400 * day;
    assert(luv::Plan10b51Validator::is_trade_permitted(plan, t0 + 95 * day, order, 1'050'000, 1000));

    std::cout << "[TEST] SEC Rule 10b5-1 Trading Plan Validator Test Passed!\n";
    return 0;
}
