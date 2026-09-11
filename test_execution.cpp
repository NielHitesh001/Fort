#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unistd.h>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_safety.hpp"

namespace {

uint64_t now_ns() {
    timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

uint32_t load_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

luv::exec::OrderIntent make_intent(uint64_t now) {
    luv::exec::OrderIntent intent {};
    intent.symbol_idx = 3;
    intent.side = luv::exec::kBuy;
    intent.qty = 100;
    intent.price = 1'234'500;
    intent.alpha_timestamp_ns = now - 50'000;
    intent.now_ns = now;
    intent.client_order_id = 0xAABBCCDDu;
    return intent;
}

struct RejectingFillCallback {
    uint32_t calls = 0;
};

[[nodiscard]] bool reject_fill_callback(
    void* context, const luv::ExecutionGateway::FillEvent&) noexcept {
    auto* const probe = static_cast<RejectingFillCallback*>(context);
    ++probe->calls;
    return false;
}

void test_branchless_risk() {
    std::printf("\n== Branchless risk ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::PreTradeRisk risk;
    assert(risk.init(arena));

    luv::exec::RiskLimits limits {};
    limits.max_order_qty = 500;
    limits.max_abs_position = 1'000;
    limits.max_alpha_age_ns = 100'000;
    risk.set_limits(3, limits);

    const uint64_t now = now_ns();
    auto intent = make_intent(now);

    auto decision = risk.evaluate(intent);
    assert(decision.pass == 1);
    assert(decision.reject_mask == luv::exec::kRejectNone);

    intent.qty = 501;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectQty) != 0);

    intent = make_intent(now);
    arena.exec_states[3].risk.net_position = 950;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectPosition) != 0);

    intent = make_intent(now);
    arena.exec_states[3].risk.net_position = 0;
    intent.alpha_timestamp_ns = now - 200'000;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectStaleAlpha) != 0);

    intent = make_intent(now);
    arena.exec_states[3].risk.halted = 1;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectHalted) != 0);

    intent = make_intent(now);
    intent.symbol_idx = luv::Config::kSymbols;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectInvalidSymbol) != 0);

    intent = make_intent(now);
    intent.price = 0;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectPrice) != 0);

    // Test price collar
    arena.exec_states[3].risk.halted = 0;
    limits.reference_price = 1'234'500;
    limits.max_price_collar_pct = 5; // 5% max deviation
    risk.set_limits(3, limits);

    intent = make_intent(now);
    intent.price = 1'234'500; // Exact match: pass
    assert(risk.evaluate(intent).pass == 1);

    intent = make_intent(now);
    intent.price = 1'500'000; // > 5% deviation: reject
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectPrice) != 0);

    std::printf("  [OK] pass, fat-finger, position, stale-alpha, halt, price collar masks\n");
}

void test_ouch_template_and_gateway() {
    std::printf("\n== OUCH templating + gateway ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits {};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    const uint64_t now = now_ns();
    const auto intent = make_intent(now);

    luv::OutboundPacket packet {};
    const auto decision = gateway.try_build(intent, packet);
    assert(decision.pass == 1);
    assert(packet.len == luv::exec::ouch::kEnterOrderLen);

    assert(packet.bytes[luv::exec::ouch::kMsgTypeOffset] == 'O');
    assert(packet.bytes[luv::exec::ouch::kSideOffset] == 'B');
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kTokenOffset) ==
           intent.client_order_id);
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kQtyOffset) == 100);
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kPriceOffset) ==
           1'234'500);
    assert(packet.bytes[luv::exec::ouch::kSymbolOffset + 0] == 'S');
    assert(packet.bytes[luv::exec::ouch::kSymbolOffset + 1] == '0');
    assert(packet.bytes[luv::exec::ouch::kSymbolOffset + 2] == '0');
    assert(packet.bytes[luv::exec::ouch::kSymbolOffset + 3] == '3');

    assert(arena.exec_states[3].risk.order_count == 1);
    assert(arena.exec_states[3].risk.net_position == 100);
    assert(arena.exec_states[3].orders[0].order_id == intent.client_order_id);

    for (uint32_t i = 1; i < luv::Config::kMaxActiveOrders; ++i) {
        auto additional = intent;
        additional.client_order_id += i;
        const auto accepted = gateway.try_build(additional, packet);
        assert(accepted.pass == 1);
    }
    auto over_capacity = intent;
    over_capacity.client_order_id = 0xCCDD0011u;
    const auto capacity_reject = gateway.try_build(over_capacity, packet);
    assert(capacity_reject.pass == 0);
    assert(packet.len == 0);
    assert(arena.exec_states[3].risk.halted == 1);
    assert(arena.exec_states[3].risk.reject_count == 1);

    auto reject = intent;
    reject.qty = 5'000;
    luv::OutboundPacket rejected_packet {};
    const auto rejected = gateway.try_build(reject, rejected_packet);
    assert(rejected.pass == 0);
    assert(rejected_packet.len == 0);
    assert(arena.exec_states[3].risk.reject_count == 2);

    auto invalid = intent;
    invalid.symbol_idx = luv::Config::kSymbols;
    const auto invalid_decision = gateway.try_build(invalid, rejected_packet);
    assert(invalid_decision.pass == 0);
    assert((invalid_decision.reject_mask & luv::exec::kRejectInvalidSymbol) != 0);

    arena.exec_states[3].risk.order_count = luv::Config::kMaxActiveOrders;
    const auto capacity_decision = gateway.try_build(intent, rejected_packet);
    assert(capacity_decision.pass == 0);
    assert((capacity_decision.reject_mask & luv::exec::kRejectOrderCapacity) != 0);

    std::printf("  [OK] fixed offsets patched and rejects suppress packet len\n");
}

void test_capacity_rejection_is_counted() {
    std::printf("\n== Capacity rejection accounting ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 1'000'000;
    limits.max_alpha_age_ns = 1'000'000;
    assert(gateway.risk().set_limits(3, limits));

    const uint64_t now = now_ns();
    const auto intent = make_intent(now);
    luv::OutboundPacket packet{};
    for (uint32_t i = 0; i < luv::Config::kMaxActiveOrders; ++i) {
        auto accepted = intent;
        accepted.client_order_id += i;
        assert(gateway.try_build(accepted, packet).pass == 1);
    }

    const uint32_t rejects_before = arena.exec_states[3].risk.reject_count;
    auto excess = intent;
    excess.client_order_id += luv::Config::kMaxActiveOrders;
    const auto rejected = gateway.try_build(excess, packet);
    assert(rejected.pass == 0);
    assert((rejected.reject_mask & luv::exec::kRejectOrderCapacity) != 0);
    assert(packet.len == 0);
    assert(arena.exec_states[3].risk.reject_count == rejects_before + 1);
    assert(arena.exec_states[3].risk.halted == 1);

    std::printf("  [OK] capacity-full admission increments rejection count\n");
}

void test_production_controls() {
    std::printf("\n== Production controls ==\n");

    luv::SequenceTracker sequence;
    assert(sequence.observe(100) == luv::SequenceResult::kFirst);
    assert(sequence.observe(102) == luv::SequenceResult::kGap);
    assert(sequence.gaps() == 1);
    assert(sequence.observe(102) == luv::SequenceResult::kDuplicate);

    luv::CircuitBreaker breaker(2);
    breaker.record_failure();
    assert(breaker.allow());
    breaker.record_failure();
    assert(!breaker.allow());
    breaker.reset();
    assert(breaker.allow());

    luv::OrderRateLimiter limiter(1);
    assert(limiter.try_acquire());
    assert(!limiter.try_acquire());
    limiter.release();
    assert(limiter.try_acquire());

    std::printf("  [OK] sequence gaps, circuit breaker, and rate limit\n");
}

void test_duplicate_order_id_fails_closed() {
    std::printf("\n== Duplicate order ID admission ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));
    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    const auto intent = make_intent(now_ns());
    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent, packet).pass == 1);

    luv::OutboundPacket duplicate_packet{};
    const auto duplicate = gateway.try_build(intent, duplicate_packet);
    assert(duplicate.pass == 0);
    assert(duplicate_packet.len == 0);
    assert(arena.exec_states[3].risk.order_count == 1);
    assert(arena.exec_states[3].orders[0].order_id == intent.client_order_id);

    std::printf("  [OK] duplicate IDs do not remove the accepted order\n");
}

void test_gateway_fail_closed_controls() {
    std::printf("\n== Gateway fail-closed controls ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway(1);
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits {};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    auto intent = make_intent(now_ns());
    luv::OutboundPacket packet {};
    auto decision = gateway.try_build(intent, packet);
    assert(decision.pass == 1);
    assert(packet.len == luv::exec::ouch::kEnterOrderLen);

    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, intent.qty, true}));

    auto second = intent;
    second.client_order_id = 0x12345678u;
    luv::OutboundPacket second_packet {};
    decision = gateway.try_build(second, second_packet);
    assert(decision.pass == 1);
    assert(second_packet.len == luv::exec::ouch::kEnterOrderLen);

    gateway.circuit_breaker().trip();
    auto after_trip = intent;
    after_trip.client_order_id = 0x9ABCDEF0u;
    luv::OutboundPacket trip_packet {};
    decision = gateway.try_build(after_trip, trip_packet);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectHalted) != 0);

    std::printf("  [OK] gateway fails closed when rate-limited or tripped\n");
}

void test_audit_admission() {
    std::printf("\n== Durable audit admission ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::DurableAuditLog audit;
    const char* path = "/tmp/luv-execution-audit-test.bin";
    ::unlink(path);
    assert(audit.open(path, 1));
    gateway.set_audit_log(&audit);

    luv::exec::RiskLimits limits {};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    const auto intent = make_intent(now_ns());
    luv::OutboundPacket packet {};
    assert(gateway.try_build(intent, packet).pass == 1);
    assert(audit.size() == 1);
    assert(gateway.fill_report_count() == 0);
    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 40, false}));
    assert(gateway.fill_report_count() == 1);
    assert(arena.exec_states[3].orders[0].filled_qty == 40);
    assert(arena.exec_states[3].orders[0].state == 2);
    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 60, true}));
    assert(gateway.fill_report_count() == 2);
    assert(arena.exec_states[3].risk.order_count == 0);
    assert(!gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 1, false}));
    assert(gateway.fill_report_count() == 2);
    audit.close();
    ::unlink(path);

    std::printf("  [OK] accepted order durably recorded\n");
}

void test_fill_notification_backpressure_accounting() {
    std::printf("\n== Fill notification backpressure accounting ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    assert(gateway.risk().set_limits(3, limits));

    RejectingFillCallback callback{};
    gateway.set_fill_event_callback(&reject_fill_callback, &callback);
    const auto intent = make_intent(now_ns());
    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent, packet).pass == 1);
    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, intent.qty, true}));

    assert(callback.calls == 1);
    assert(gateway.fill_report_count() == 1);
    assert(gateway.dropped_fill_notifications() == 1);
    gateway.set_fill_event_callback(nullptr, nullptr);
    std::printf("  [OK] bounded fill notification failure is counted without retrying\n");
}

void test_terminal_partial_fill_releases_reservation() {
    std::printf("\n== Terminal partial fill reservation release ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    const char* ledger_path = "/tmp/luv-terminal-partial-fill.bin";
    ::unlink(ledger_path);
    luv::RecoveryLedger ledger;
    assert(ledger.open(ledger_path));
    gateway.set_recovery_ledger(&ledger);

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    assert(gateway.risk().set_limits(3, limits));

    const auto intent = make_intent(now_ns());
    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent, packet).pass == 1);
    assert(arena.exec_states[3].risk.net_position == intent.qty);
    assert(arena.exec_states[3].risk.gross_exposure ==
           intent.qty * intent.price);

    // The venue filled 40 and terminally cancelled the remaining 60. The
    // settled position must retain only the executed quantity.
    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 40, true}));
    // Add + one terminal transition: terminal partial recovery is a single
    // durable record, not a crash-visible Fill followed by Cancel pair.
    assert(ledger.size() == 2);
    const luv::ActiveOrder& terminal = arena.exec_states[3].orders[0];
    assert(terminal.state == 3);
    assert(terminal.filled_qty == 40 && terminal.qty == 60);
    assert(arena.exec_states[3].risk.order_count == 0);
    assert(arena.exec_states[3].risk.net_position == 40);
    assert(arena.exec_states[3].risk.gross_exposure == 40 * intent.price);

    luv::RecoveredOrder recovered[1]{};
    uint32_t recovered_count = 0;
    assert(ledger.replay(recovered, 1, recovered_count));
    assert(recovered_count == 0);

    // A duplicate cancel report or a REST cancel after terminal completion is
    // a benign not-found result; neither path may halt the symbol.
    assert(!gateway.apply_cancel_report(3, intent.client_order_id));
    assert(!gateway.cancel_order(intent.client_order_id));
    const uint32_t rejects_before = arena.exec_states[3].risk.reject_count;
    const int64_t net_before = arena.exec_states[3].risk.net_position;
    const int64_t gross_before = arena.exec_states[3].risk.gross_exposure;
    const uint32_t active_before = arena.exec_states[3].risk.order_count;
    // A delayed reject after this terminal partial fill is stale. It must be
    // a no-op rather than clearing the retained terminal snapshot or
    // accounting the unfilled remainder a second time.
    assert(!gateway.apply_reject_report(3, intent.client_order_id, 'R'));
    assert(terminal.state == 3 && terminal.filled_qty == 40 && terminal.qty == 60);
    assert(arena.exec_states[3].risk.reject_count == rejects_before);
    assert(arena.exec_states[3].risk.net_position == net_before);
    assert(arena.exec_states[3].risk.gross_exposure == gross_before);
    assert(arena.exec_states[3].risk.order_count == active_before);
    assert(!gateway.circuit_breaker().tripped());
    assert(!arena.exec_states[3].risk.halted);
    ledger.close();
    ::unlink(ledger_path);
    std::printf("  [OK] terminal partial release and duplicate cancel are safe\n");
}

void test_recovery_failure_preserves_live_order() {
    std::printf("\n== Recovery failure preserves live order ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::RecoveryLedger ledger;
    const char* path = "/tmp/luv-execution-recovery-failure-test.bin";
    ::unlink(path);
    assert(ledger.open(path));
    gateway.set_recovery_ledger(&ledger);

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    const auto intent = make_intent(now_ns());
    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent, packet).pass == 1);
    ledger.close();

    assert(!gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 40, true}));
    assert(arena.exec_states[3].orders[0].filled_qty == 0);
    assert(arena.exec_states[3].orders[0].qty == intent.qty);
    assert(arena.exec_states[3].orders[0].state == 1);
    assert(arena.exec_states[3].risk.net_position == intent.qty);
    assert(arena.exec_states[3].risk.gross_exposure == intent.qty * intent.price);

    ::unlink(path);
    std::printf("  [OK] failed terminal transition leaves live order unchanged\n");
}

void test_order_replace_flow() {
    std::printf("\n== Order replace flow ==\n");

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits {};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    const uint64_t now = now_ns();
    auto intent = make_intent(now);
    intent.time_in_force = luv::exec::kIOC;
    luv::OutboundPacket packet {};
    assert(gateway.try_build(intent, packet).pass == 1);
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kTimeInForceOffset) == luv::exec::kIOC);

    // Try replace on active order
    luv::exec::OrderReplaceIntent replace_intent {};
    replace_intent.symbol_idx = 3;
    replace_intent.original_order_id = intent.client_order_id;
    replace_intent.replacement_order_id = 0xAABBCCDEu;
    replace_intent.new_qty = 150;
    replace_intent.new_price = 1'250'000;
    replace_intent.alpha_timestamp_ns = now;
    replace_intent.now_ns = now;
    replace_intent.time_in_force = luv::exec::kDay;

    luv::OutboundPacket replace_packet {};
    assert(gateway.try_replace(replace_intent, replace_packet).pass == 1);
    assert(replace_packet.len == luv::exec::ouch::kReplaceOrderLen);
    assert(replace_packet.bytes[luv::exec::ouch::kReplaceMsgTypeOffset] == 'U');
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplaceExistingTokenOffset) == intent.client_order_id);
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplaceReplacementTokenOffset) == replace_intent.replacement_order_id);
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplaceQtyOffset) == 150);
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplacePriceOffset) == 1'250'000);

    // Apply replace report
    assert(gateway.apply_replace_report(3, intent.client_order_id, 150, 1'250'000));
    assert(arena.exec_states[3].orders[0].qty == 150);
    assert(arena.exec_states[3].orders[0].price == 1'250'000);
    assert(arena.exec_states[3].risk.net_position == 150);

    std::printf("  [OK] order replace built and applied successfully\n");
}

void test_circuit_breaker_cooldown() {
    std::printf("\n== Circuit breaker cool-down and auto-reset ==\n");

    luv::CircuitBreaker breaker(2, 500'000); // 2 failures, 500us cool-down
    assert(breaker.allow(1'000'000));

    breaker.record_failure(1'000'000);
    assert(!breaker.tripped());
    assert(breaker.allow(1'000'100));

    breaker.record_failure(1'000'200);
    assert(breaker.tripped());
    assert(!breaker.allow(1'000'300)); // Within cool-down (100us < 500us)

    // After cool-down period
    assert(breaker.allow(1'000'200 + 500'000 + 1));
    assert(!breaker.tripped()); // Auto-reset

    std::printf("  [OK] circuit breaker cool-down and auto-reset verified\n");
}

void benchmark_risk_core() {
    std::printf("\n== Risk timing sample ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::PreTradeRisk risk;
    assert(risk.init(arena));
    luv::exec::RiskLimits limits {};
    limits.max_order_qty = 64;
    limits.max_abs_position = 1'000'000;
    limits.max_alpha_age_ns = 1'000'000;
    risk.set_limits(3, limits);

    auto intent = make_intent(now_ns());
    constexpr uint32_t kIters = 5'000'000;
    volatile uint32_t sink = 0;

    const uint64_t start = now_ns();
    for (uint32_t i = 0; i < kIters; ++i) {
        intent.qty = 1 + static_cast<int64_t>(i & 127u);
        intent.now_ns = intent.alpha_timestamp_ns + 50'000 + (i & 7u);
        const auto d = risk.evaluate(intent);
        sink += static_cast<uint32_t>(d.pass) + d.reject_mask;
    }
    const uint64_t elapsed = now_ns() - start;
    const double ns_per_eval = static_cast<double>(elapsed) / kIters;

    std::printf("  avg risk evaluate: %.2f ns (%u pass sink)\n",
                ns_per_eval, static_cast<uint32_t>(sink));
    assert(sink > 0);
}

}  // namespace

int main() {
    std::printf("Execution Gateway Test\n");

    test_branchless_risk();
    test_ouch_template_and_gateway();
    test_capacity_rejection_is_counted();
    test_order_replace_flow();
    test_circuit_breaker_cooldown();
    test_production_controls();
    test_duplicate_order_id_fails_closed();
    test_gateway_fail_closed_controls();
    test_audit_admission();
    test_fill_notification_backpressure_accounting();
    test_terminal_partial_fill_releases_reservation();
    test_recovery_failure_preserves_live_order();
    benchmark_risk_core();

    std::printf("\nAll execution tests passed.\n");
    return 0;
}
