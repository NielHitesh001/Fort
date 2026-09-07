#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "luv_execution.hpp"
#include "luv_recovery.hpp"
#include "luv_telemetry.hpp"

namespace {

constexpr const char* kLedgerPath = "/tmp/luv_day4_integration.log";

uint64_t now_ns() {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

void reset_ledger_path() {
    ::unlink(kLedgerPath);
}

}  // namespace

int main() {
    reset_ledger_path();

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::RecoveryLedger ledger;
    assert(ledger.open(kLedgerPath));
    gateway.set_recovery_ledger(&ledger);

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    luv::exec::OrderIntent intent{};
    intent.symbol_idx = 3;
    intent.side = luv::exec::kBuy;
    intent.qty = 100;
    intent.price = 1'234'500;
    intent.alpha_timestamp_ns = now_ns() - 50'000;
    intent.now_ns = now_ns();
    intent.client_order_id = 0x1001u;

    luv::OutboundPacket packet{};
    const auto accepted = gateway.try_build(intent, packet);
    assert(accepted.pass == 1);
    assert(packet.len == luv::exec::ouch::kEnterOrderLen);
    assert(ledger.size() == 1);
    assert(arena.exec_states[3].risk.order_count == 1);

    luv::TelemetryBatchCollector<256> collector;
    collector.record_order_sent();
    collector.record_order_check(true);
    collector.record_ack();
    collector.record_fill(1'000);
    collector.set_position(100);
    assert(collector.flush(arena));

    const luv::TelemSnapshot* snap = arena.telem_ring.try_peek();
    assert(snap != nullptr);
    assert(snap->fill_count == 1);
    assert(snap->reject_count == 0);
    assert(snap->active_orders == 0 || snap->active_orders == 1);

    luv::ExecutionReport fill_report{};
    fill_report.order_id = intent.client_order_id;
    fill_report.filled_quantity = 100;
    fill_report.terminal = true;
    assert(gateway.apply_execution_report(3, fill_report));
    assert(ledger.size() >= 2);

    std::printf("day4 integration check: risk gate + ledger + telemetry + fill accepted\n");
    return 0;
}
