#include "luv_margin_call_watchdog.hpp"
#include <cassert>
#include <cstdio>

void test_margin_call_watchdog_lifecycle() {
    luv::risk::MarginCallWatchdog watchdog;

    // Account 101 initial state:
    // Equity: $100,000 (1000000000), IM: $80,000 (800000000), MM: $50,000 (500000000)
    assert(watchdog.update_account(101, 1'000'000'000, 800'000'000, 500'000'000, 1'000'000));
    assert(watchdog.get_account_state(101) == luv::risk::MarginHealthState::kHealthy);

    // Adverse market shock: Equity drops to $70,000 (< IM $80k, > MM $50k) -> Margin Call Issued
    assert(watchdog.update_account(101, 700'000'000, 800'000'000, 500'000'000, 2'000'000));
    assert(watchdog.get_account_state(101) == luv::risk::MarginHealthState::kMarginCallIssued);

    // Severe flash crash: Equity drops to $40,000 (< MM $50k) -> Immediate Liquidation
    assert(watchdog.update_account(101, 400'000'000, 800'000'000, 500'000'000, 3'000'000));
    assert(watchdog.get_account_state(101) == luv::risk::MarginHealthState::kForcedLiquidationBreach);

    std::printf("[PASS] test_margin_call_watchdog_lifecycle\n");
}

int main() {
    test_margin_call_watchdog_lifecycle();
    std::printf("All margin call watchdog tests passed successfully.\n");
    return 0;
}
