#include "luv_rule_13h.hpp"
#include <cassert>
#include <cstdio>

void test_sec_rule_13h_large_trader_identification() {
    luv::compliance::Rule13hLargeTraderEngine engine;

    // Register Account 101 (Retail / Small Fund - no initial LTID)
    assert(engine.register_account(101, 0));
    assert(!engine.is_large_trader(101));

    // Execute 500,000 shares @ $10.00 ($5,000,000 dollar volume) -> Below 2M shares / $20M daily threshold
    assert(engine.record_execution(101, 10'0000LL, 500'000));
    assert(!engine.is_large_trader(101));

    // Execute an additional 1,600,000 shares @ $10.00 -> Total 2,100,000 shares (> 2,000,000 threshold)
    assert(engine.record_execution(101, 10'0000LL, 1'600'000));
    // Large Trader threshold triggered
    assert(engine.is_large_trader(101));

    std::printf("[PASS] test_sec_rule_13h_large_trader_identification (Triggered Large Trader Status on Account 101)\n");
}

int main() {
    test_sec_rule_13h_large_trader_identification();
    std::printf("All SEC Rule 13h-1 Large Trader tests passed successfully.\n");
    return 0;
}
