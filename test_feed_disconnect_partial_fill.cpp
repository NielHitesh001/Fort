#include "luv_test_harness.hpp"
#include <chrono>
#include <cstdio>

int main() {
    luv::test::FailureHarness harness;
    assert(harness.poll_feed() > 0);
    assert(harness.submit(1));
    assert(harness.gateway.apply_execution_report(0, {1, 50, false}));
    harness.feed_connected = false;
    const auto start = std::chrono::steady_clock::now();
    const auto before = harness.arena.tick_ring.size();
    assert(harness.poll_feed() == 0);
    assert(harness.arena.tick_ring.size() == before);
    luv::ActiveOrder order{};
    assert(harness.gateway.query_order(1, order));
    assert(order.filled_qty == 50 && order.qty == 50 && order.state == 2);
    luv::RecoveredOrder recovered[2]{};
    uint32_t count = 0;
    assert(harness.ledger.replay(recovered, 2, count));
    assert(count == 1 && recovered[0].order_id == 1 && recovered[0].remaining == 50);
    harness.feed_connected = true;
    assert(harness.poll_feed() > 0);
    assert(harness.gateway.apply_execution_report(0, {1, 50, true}));
    assert(harness.gateway.query_order(1, order));
    assert(order.filled_qty == 100 && order.qty == 0 && order.state == 3);
    assert(harness.submit(2));
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("Synthetic feed pause/partial-fill/replay/resume: %lld us (local diagnostic only)\n",
        static_cast<long long>(elapsed));
}
