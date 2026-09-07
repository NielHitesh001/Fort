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
    intent.alpha_timestamp_ns = now + 1;
    arena.exec_states[3].risk.halted = 0;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);
    assert((decision.reject_mask & luv::exec::kRejectStaleAlpha) != 0);

    intent.symbol_idx = luv::Config::kSymbols;
    decision = risk.evaluate(intent);
    assert(decision.pass == 0);

    std::printf("  [OK] input, position, alpha, and halt validation\n");
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

    auto reject = intent;
    reject.qty = 5'000;
    luv::OutboundPacket rejected_packet {};
    const auto rejected = gateway.try_build(reject, rejected_packet);
    assert(rejected.pass == 0);
    assert(rejected_packet.len == 0);
    assert(arena.exec_states[3].risk.reject_count == 1);

    std::printf("  [OK] fixed offsets patched and rejects suppress packet len\n");
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
    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 40, false}));
    assert(arena.exec_states[3].orders[0].filled_qty == 40);
    assert(arena.exec_states[3].orders[0].state == 2);
    assert(gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 60, true}));
    assert(arena.exec_states[3].risk.order_count == 0);
    assert(!gateway.apply_execution_report(
        3, luv::ExecutionReport{intent.client_order_id, 1, false}));
    audit.close();
    ::unlink(path);

    std::printf("  [OK] accepted order durably recorded\n");
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
    test_production_controls();
    test_audit_admission();
    benchmark_risk_core();

    std::printf("\nAll execution tests passed.\n");
    return 0;
}
