#include <cassert>
#include <cstdint>
#include <cstdio>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_safety.hpp"

namespace {

void test_state_transitions_and_half_open() {
    std::printf("\n== Multi-tier States and Half-Open Probing ==\n");

    luv::CircuitBreaker breaker(2, 500'000); // 2 failures, 500us cooldown
    breaker.set_half_open_params(1, 1); // 1 probe, 1 success needed

    assert(breaker.state() == luv::CircuitBreakerState::kClosed);
    assert(breaker.is_closed());
    assert(breaker.allow(1'000'000));

    // 1 failure: remains closed
    breaker.record_failure(1'000'000);
    assert(breaker.is_closed());
    assert(breaker.failures() == 1);

    // 2nd failure: trips to Open
    breaker.record_failure(1'000'100);
    assert(breaker.is_open());
    assert(breaker.tripped());
    assert(!breaker.allow(1'000'200)); // In cooldown (100us < 500us)

    // After cooldown: allow() transitions to HalfOpen and allows 1 probe
    assert(breaker.allow(1'000'100 + 500'000 + 1));
    assert(breaker.is_half_open());

    // 2nd simultaneous call in HalfOpen should be blocked because max_probes = 1
    assert(!breaker.allow(1'000'100 + 500'000 + 2));

    // Successful probe completes test and re-arms to Closed
    breaker.record_success();
    assert(breaker.is_closed());
    assert(!breaker.tripped());
    assert(breaker.failures() == 0);

    // Now test probe failure: trips immediately back to Open
    breaker.trip(2'000'000);
    assert(breaker.is_open());
    assert(breaker.allow(2'500'001)); // transitions to HalfOpen
    assert(breaker.is_half_open());

    breaker.record_failure(2'500'002); // probe failed!
    assert(breaker.is_open()); // immediately open again
    assert(breaker.trip_timestamp_ns() == 2'500'002);

    std::printf("  [OK] Closed -> Open -> HalfOpen -> Closed and failed probe re-trip verified\n");
}

void test_consecutive_rejections_and_loss_limits() {
    std::printf("\n== Consecutive Rejections & Loss Limit Tripping ==\n");

    luv::CircuitBreaker breaker(5, 100'000);
    breaker.set_max_consecutive_rejections(3); // 3 rejections -> trip
    breaker.set_max_loss_limit(1'000'000); // 100.00 max loss

    assert(breaker.is_closed());

    // Record rejections
    breaker.record_rejection(10);
    assert(breaker.is_closed());
    assert(breaker.consecutive_rejections() == 1);

    breaker.record_rejection(20);
    assert(breaker.is_closed());
    assert(breaker.consecutive_rejections() == 2);

    breaker.record_rejection(30); // 3rd rejection trips!
    assert(breaker.is_open());

    breaker.reset();
    assert(breaker.is_closed());
    assert(breaker.consecutive_rejections() == 0);

    // Test loss limit check
    assert(breaker.check_loss_limit(500'000, 100));
    assert(breaker.is_closed());

    assert(!breaker.check_loss_limit(1'000'000, 200)); // breaches limit
    assert(breaker.is_open());

    std::printf("  [OK] consecutive rejections and gross loss breach trigger breaker\n");
}

void test_burst_rate_limits() {
    std::printf("\n== Burst Message Rate Limiting ==\n");

    luv::CircuitBreaker breaker(1, 100'000);
    // Allow max 5 messages per 1 millisecond (1'000'000 ns)
    breaker.set_rate_limit(5, 1'000'000);

    const uint64_t start_time = 10'000'000;
    for (int i = 0; i < 5; ++i) {
        assert(breaker.allow(start_time + (i * 100)));
        assert(breaker.is_closed());
    }

    // 6th message within the same millisecond trips burst rate
    assert(!breaker.allow(start_time + 600));
    assert(breaker.is_open());

    std::printf("  [OK] burst rate limit per window properly enforced\n");
}

void test_per_symbol_granular_halting() {
    std::printf("\n== Per-Symbol Granular Halting ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    limits.max_consecutive_rejections = 2; // halt symbol after 2 rejections
    gateway.risk().set_limits(3, limits);
    gateway.risk().set_limits(7, limits);

    assert(!gateway.is_symbol_halted(3));
    assert(!gateway.is_symbol_halted(7));

    // Submit valid order for symbol 3
    luv::exec::OrderIntent intent3{};
    intent3.symbol_idx = 3;
    intent3.side = luv::exec::kBuy;
    intent3.qty = 100;
    intent3.price = 1'000'000;
    intent3.alpha_timestamp_ns = 100;
    intent3.now_ns = 100;
    intent3.client_order_id = 301;

    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent3, packet).pass == 1);

    // Halt symbol 3 only
    gateway.halt_symbol(3);
    assert(gateway.is_symbol_halted(3));
    assert(!gateway.is_symbol_halted(7)); // Symbol 7 remains unhalted!

    // Symbol 3 orders are rejected
    intent3.client_order_id = 302;
    auto decision3 = gateway.try_build(intent3, packet);
    assert(decision3.pass == 0);
    assert((decision3.reject_mask & luv::exec::kRejectHalted) != 0);

    // Symbol 7 orders continue to pass normally
    luv::exec::OrderIntent intent7{};
    intent7.symbol_idx = 7;
    intent7.side = luv::exec::kSell;
    intent7.qty = 50;
    intent7.price = 2'000'000;
    intent7.alpha_timestamp_ns = 100;
    intent7.now_ns = 100;
    intent7.client_order_id = 701;

    auto decision7 = gateway.try_build(intent7, packet);
    assert(decision7.pass == 1);
    assert(packet.len == luv::exec::ouch::kEnterOrderLen);

    // Resume symbol 3
    gateway.resume_symbol(3);
    assert(!gateway.is_symbol_halted(3));
    intent3.client_order_id = 303;
    assert(gateway.try_build(intent3, packet).pass == 1);

    std::printf("  [OK] per-symbol halting operates independently without halting global gateway\n");
}

}  // namespace

int main() {
    std::printf("=== Advanced Circuit Breaker & Pre-Trade Risk Tests ===\n");

    test_state_transitions_and_half_open();
    test_consecutive_rejections_and_loss_limits();
    test_burst_rate_limits();
    test_per_symbol_granular_halting();

    std::printf("\nAll Advanced Circuit Breaker tests passed successfully.\n");
    return 0;
}
