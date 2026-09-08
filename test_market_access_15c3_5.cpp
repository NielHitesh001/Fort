#include "luv_market_access_15c3_5.hpp"
#include <cassert>
#include <cstdio>

void test_sec_rule_15c3_5_pre_trade_risk() {
    luv::reg_15c3_5::MarketAccessRule15c35Validator validator;

    // Register Account #1001 with:
    // Max single order notional: $500,000 (500000 * 10000 = 5'000'000'000)
    // Max single order qty: 10,000 shares
    // Gross credit limit: $2,000,000 (2000000 * 10000 = 20'000'000'000)
    assert(validator.register_account(luv::reg_15c3_5::AccountCreditProfile{
        .account_id = 1001,
        .max_single_order_notional = 5'000'000'000LL, // $500,000 * 10,000
        .max_single_order_qty = 10'000,
        .gross_credit_limit = 20'000'000'000LL,       // $2,000,000 * 10,000
        .accumulated_gross_notional = 0,
        .is_blocked = false
    }));

    // 1. Valid order: Buy 1,000 shares @ $100.00 ($100,000 notional) -> Pass
    auto r1 = validator.validate_and_reserve(1001, 1000000, 1000);
    assert(r1 == luv::reg_15c3_5::MarketAccessRejectReason::kNone);

    // 2. Quantity breach: Buy 15,000 shares (Limit: 10,000) -> Reject
    auto r2 = validator.validate_and_reserve(1001, 1000000, 15000);
    assert(r2 == luv::reg_15c3_5::MarketAccessRejectReason::kSingleOrderQuantityExceeded);

    // 3. Single-order notional breach: Buy 6,000 shares @ $100.00 ($600,000 > $500,000 limit) -> Reject
    auto r3 = validator.validate_and_reserve(1001, 1000000, 6000);
    assert(r3 == luv::reg_15c3_5::MarketAccessRejectReason::kSingleOrderNotionalExceeded);

    // 4. Unknown/blocked account -> Reject
    auto r4 = validator.validate_and_reserve(9999, 1000000, 100);
    assert(r4 == luv::reg_15c3_5::MarketAccessRejectReason::kAccountBlocked);

    std::printf("[PASS] test_sec_rule_15c3_5_pre_trade_risk\n");
}

int main() {
    test_sec_rule_15c3_5_pre_trade_risk();
    std::printf("All SEC Rule 15c3-5 Market Access tests passed successfully.\n");
    return 0;
}
