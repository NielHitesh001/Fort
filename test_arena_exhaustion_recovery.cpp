#include "luv_test_harness.hpp"
#include <array>
#include <cstdio>

int main() {
    luv::test::FailureHarness harness;
    // Exhaust the actual per-symbol active-order slab. This does not pretend
    // that consuming the entire 13 GiB AI mapping is an order admission test.
    for (uint32_t i = 1; i <= luv::Config::kMaxActiveOrders; ++i) assert(harness.submit(i));
    assert(harness.gateway.apply_execution_report(0, {1, 50, false}));
    const auto risk_before = harness.arena.exec_states[0].risk;
    assert(!harness.submit(1000));
    const auto& risk = harness.arena.exec_states[0].risk;
    assert(risk.order_count == risk_before.order_count);
    assert(risk.net_position == risk_before.net_position);
    assert(risk.gross_exposure == risk_before.gross_exposure);
    luv::ActiveOrder order{};
    assert(harness.gateway.query_order(1, order) && order.filled_qty == 50 && order.qty == 50);
    assert(!harness.gateway.query_order(1000, order));
    std::array<luv::RecoveredOrder, luv::Config::kMaxActiveOrders> recovered{};
    uint32_t count = 0;
    assert(harness.ledger.replay(recovered.data(), recovered.size(), count));
    assert(count == luv::Config::kMaxActiveOrders && recovered[0].remaining == 50);
    assert(harness.gateway.apply_execution_report(0, {1, 50, true}));
    assert(!harness.submit(1001)); // Exhaustion latched the symbol halt.
    harness.gateway.resume_symbol(0); // Operator action after reconciliation.
    assert(harness.submit(1001));
    harness.ledger.close();
    assert(harness.ledger.open(harness.ledger_path));
    assert(harness.ledger.replay(recovered.data(), recovered.size(), count));
    assert(count == luv::Config::kMaxActiveOrders);
    std::puts("Active-order slab exhaustion: partial fill preserved; explicit resume, slot reuse and WAL reopen passed.");
}
