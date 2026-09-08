#include "luv_10b18.hpp"
#include <cassert>
#include <cstdio>

void test_sec_rule_10b18_safe_harbor() {
    luv::compliance::Rule10b18SafeHarborEngine engine;

    // Symbol 1 (AAPL):
    // 4-Week ADTV: 1,000,000 shares -> 25% max buyback volume = 250,000 shares
    // Session: 9:30 AM to 4:00 PM (open: 1'000'000'000ns, close: 24'400'000'000'000ns)
    // Broker ID: 101 (Single broker-dealer rule)
    assert(engine.register_symbol(luv::compliance::Rule10b18DailyProfile{
        .symbol_idx = 1,
        .authorized_broker_id = 101,
        .four_week_adtv = 1'000'000,
        .accumulated_repurchase_qty = 0,
        .market_open_ns = 1'000'000'000ULL,
        .market_close_ns = 24'400'000'000'000ULL
    }));

    uint64_t valid_time = 5'000'000'000'000ULL; // Mid-day

    // 1. Multiple Broker Violation: Broker 202 tries to buyback
    auto r1 = engine.validate_buyback_order(1, 202, 1500000, 10000, 1500000, 1500000, valid_time);
    assert(r1 == luv::compliance::Rule10b18RejectReason::kMultipleBrokersViolated);

    // 2. Timing Violation: Try to buyback 5 mins after open (1'300'000'000ns < 1'600'000'000ns)
    auto r2 = engine.validate_buyback_order(1, 101, 1500000, 10000, 1500000, 1500000, 1'300'000'000ULL);
    assert(r2 == luv::compliance::Rule10b18RejectReason::kTimingConditionViolated);

    // 3. Price Violation: Independent Bid = $150.00, Last Sale = $150.00, Order = $150.50
    auto r3 = engine.validate_buyback_order(1, 101, 1505000, 10000, 1500000, 1500000, valid_time);
    assert(r3 == luv::compliance::Rule10b18RejectReason::kPriceConditionViolated);

    // 4. Volume Violation: Try to buy 300,000 shares (> 250,000 25% ADTV cap)
    auto r4 = engine.validate_buyback_order(1, 101, 1500000, 300000, 1500000, 1500000, valid_time);
    assert(r4 == luv::compliance::Rule10b18RejectReason::kVolumeCapExceeded);

    // 5. Valid Safe Harbor Order: Buy 50,000 shares @ $150.00
    auto r5 = engine.validate_buyback_order(1, 101, 1500000, 50000, 1500000, 1500000, valid_time);
    assert(r5 == luv::compliance::Rule10b18RejectReason::kNone);

    std::printf("[PASS] test_sec_rule_10b18_safe_harbor\n");
}

int main() {
    test_sec_rule_10b18_safe_harbor();
    std::printf("All SEC Rule 10b-18 safe harbor tests passed successfully.\n");
    return 0;
}
