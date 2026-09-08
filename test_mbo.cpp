#include "luv_mbo.hpp"
#include <cassert>
#include <cstdio>

void test_mbo_order_lifecycle() {
    luv::mbo::Level3MarketByOrderBook mbo;
    uint16_t sym = 1;

    // Add 3 orders at price 15000:
    // Order 1: qty=100, ts=100
    assert(mbo.add_order(101, sym, luv::exec::kBuy, 15000, 100, 100));
    // Order 2: qty=200, ts=200
    assert(mbo.add_order(102, sym, luv::exec::kBuy, 15000, 200, 200));
    // Order 3: qty=300, ts=300
    assert(mbo.add_order(103, sym, luv::exec::kBuy, 15000, 300, 300));
    assert(mbo.order_count() == 3);

    // Check queue ahead
    assert(mbo.compute_queue_ahead(101) == 0);   // First in queue
    assert(mbo.compute_queue_ahead(102) == 100); // 100 shares ahead
    assert(mbo.compute_queue_ahead(103) == 300); // 100 + 200 = 300 shares ahead

    // Execute partial fill on Order 1 (50 shares)
    assert(mbo.execute_order(101, 50));
    assert(mbo.compute_queue_ahead(102) == 50);  // Now 50 shares ahead
    assert(mbo.compute_queue_ahead(103) == 250); // 50 + 200 = 250 shares ahead

    // Cancel Order 2
    assert(mbo.cancel_order(102));
    assert(mbo.order_count() == 2);
    assert(mbo.compute_queue_ahead(103) == 50); // Only Order 1's remaining 50 ahead

    // Modify Order 1 to increase quantity (should lose queue priority to Order 3 if new ts > Order 3)
    assert(mbo.modify_order(101, 500, 15000, 400));
    assert(mbo.compute_queue_ahead(101) == 300); // Now Order 3 (300 shares, ts=300) is ahead of Order 1 (ts=400)!
    assert(mbo.compute_queue_ahead(103) == 0);   // Order 3 is now first!

    std::printf("[PASS] test_mbo_order_lifecycle\n");
}

int main() {
    test_mbo_order_lifecycle();
    std::printf("All Level 3 / Market by Order tests passed successfully.\n");
    return 0;
}
