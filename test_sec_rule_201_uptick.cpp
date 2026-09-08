#include "luv_sec_rule_201_uptick.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting SEC Rule 201 Short Sale Circuit Breaker Tests..." << std::endl;

    luv::SECRule201UptickEngine engine;

    // Register TSLA: Prev Close = $200.00 -> Trigger at <= $180.00 (10% drop)
    assert(engine.register_symbol("TSLA", 200.00));

    // 1. Initial State: Normal Trading (No Circuit Breaker)
    // Short sale allowed at any price, even at/below bid
    auto res1 = engine.validate_short_order("TSLA", luv::ShortSaleType::Short, 195.00, 195.00);
    assert(res1.order_permitted);
    assert(!res1.rule_201_active);

    // 2. Price Drops to $185.00 (7.5% drop) -> Still not triggered
    assert(!engine.on_price_update("TSLA", 185.00, 1'000'000'000ULL, 1));
    assert(!engine.is_circuit_breaker_active("TSLA"));

    // 3. Price Drops to $179.50 (10.25% drop) -> Triggers Rule 201 Circuit Breaker!
    assert(engine.on_price_update("TSLA", 179.50, 2'000'000'000ULL, 1));
    assert(engine.is_circuit_breaker_active("TSLA"));

    // 4. Test Short Sale Orders under Active Rule 201:
    // National Best Bid (NBB) is $179.00
    // Case A: Short Order priced at $179.00 (at the bid) -> REJECTED
    auto res_bid = engine.validate_short_order("TSLA", luv::ShortSaleType::Short, 179.00, 179.00);
    std::cout << "  Short at NBB ($179.00) Permitted: " << (res_bid.order_permitted ? "YES" : "NO") 
              << ", Reason: " << res_bid.violation_reason << std::endl;
    assert(!res_bid.order_permitted);
    assert(res_bid.rule_201_active);
    assert(std::strcmp(res_bid.violation_reason, "RULE_201_PRICE_TEST_VIOLATION") == 0);

    // Case B: Short Order priced at $178.50 (below the bid) -> REJECTED
    auto res_below = engine.validate_short_order("TSLA", luv::ShortSaleType::Short, 178.50, 179.00);
    assert(!res_below.order_permitted);

    // Case C: Short Order priced at $179.01 (above the bid) -> PERMITTED
    auto res_above = engine.validate_short_order("TSLA", luv::ShortSaleType::Short, 179.01, 179.00);
    std::cout << "  Short strictly above NBB ($179.01) Permitted: " << (res_above.order_permitted ? "YES" : "NO") << std::endl;
    assert(res_above.order_permitted);

    // Case D: Short Exempt Order priced at $178.50 (marked exempt under Rule 201(d)) -> PERMITTED
    auto res_exempt = engine.validate_short_order("TSLA", luv::ShortSaleType::ShortExempt, 178.50, 179.00);
    assert(res_exempt.order_permitted);
    assert(res_exempt.is_short_exempt);

    // 5. Day Rollover:
    // Day 1 triggered -> remains active on Day 2 -> expires on Day 3
    engine.on_day_rollover(2); // Day 2
    assert(engine.is_circuit_breaker_active("TSLA"));

    engine.on_day_rollover(3); // Day 3
    assert(!engine.is_circuit_breaker_active("TSLA")); // Cleared!

    std::cout << "[PASS] SEC Rule 201 Short Sale Circuit Breaker Tests Passed!" << std::endl;
    return 0;
}
