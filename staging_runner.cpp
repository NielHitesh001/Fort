#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_recovery.hpp"
#include "luv_telemetry.hpp"

namespace {

constexpr uint32_t kOrderCount = 100;
constexpr const char* kLedgerPath = "/tmp/luv_staging_ledger.bin";

struct StagingConfig {
    uint32_t orders = kOrderCount;
    uint32_t rate_hz = 10;
};

struct SyntheticOrder {
    uint32_t qty = 0;
    int64_t price = 0;
    uint8_t side = 0;
    uint32_t client_id = 0;
};

class SyntheticSignalGenerator {
public:
    explicit SyntheticSignalGenerator(uint32_t total) : total_(total) {}

    bool has_next() const noexcept { return generated_ < total_; }

    SyntheticOrder next() noexcept {
        const uint32_t n = generated_++;
        SyntheticOrder o{};
        o.qty = 100 + (n % 50);
        o.price = 1'000'000 + static_cast<int64_t>((n % 250) * 1000);
        o.side = (n % 2 == 0) ? luv::exec::kBuy : luv::exec::kSell;
        o.client_id = 1000 + n;
        return o;
    }

private:
    uint32_t generated_ = 0;
    uint32_t total_ = 0;
};

class ExchangeSimulator {
public:
    void start() noexcept { running_.store(true, std::memory_order_release); }
    void stop() noexcept { running_.store(false, std::memory_order_release); }

    void process_order(const luv::OutboundPacket& packet) noexcept {
        if (packet.len == 0) return;
        (void)packet;
    }

private:
    std::atomic<bool> running_{false};
};

bool verify_counts_and_replay(luv::ExecutionGateway& gateway,
                              luv::TelemetryBatchCollector<256>& collector,
                              luv::RecoveryLedger& ledger,
                              uint32_t expected_sent,
                              uint32_t expected_approved,
                              uint32_t expected_rejected,
                              uint32_t expected_filled) {
    (void)gateway;

    const luv::TelemetryBatch& batch = collector.batch();
    const bool exact = (batch.orders_sent == expected_sent) &&
                       (batch.orders_approved == expected_approved) &&
                       (batch.orders_rejected == expected_rejected) &&
                       (batch.orders_filled == expected_filled) &&
                       (batch.orders_filled <= batch.orders_approved);

    std::printf("synthetic telemetry: sent=%u approved=%u rejected=%u filled=%u exact=%s\n",
                 batch.orders_sent,
                 batch.orders_approved,
                 batch.orders_rejected,
                 batch.orders_filled,
                 exact ? "PASS" : "FAIL");

    if (!exact) return false;

    if (!ledger.open(kLedgerPath)) {
        std::printf("ledger replay open failed\n");
        return false;
    }

    luv::RecoveredOrder orders[8]{};
    uint32_t count = 0;
    const bool replay_ok = ledger.replay(orders, 8, count);
    std::printf("ledger replay ok=%s count=%u\n", replay_ok ? "PASS" : "FAIL", count);
    return replay_ok && count > 0;
}

}  // namespace

int main() {
    ::unlink(kLedgerPath);

    luv::Arena arena;
    if (!arena.init()) {
        std::printf("arena init failed\n");
        return 1;
    }

    luv::ExecutionGateway gateway;
    if (!gateway.init(arena)) {
        std::printf("gateway init failed\n");
        return 1;
    }

    luv::RecoveryLedger ledger;
    if (!ledger.open(kLedgerPath)) {
        std::printf("ledger open failed\n");
        return 1;
    }
    gateway.set_recovery_ledger(&ledger);

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    luv::TelemetryBatchCollector<256> collector;
    ExchangeSimulator sim;
    sim.start();

    SyntheticSignalGenerator generator(kOrderCount);
    uint32_t sent = 0;
    uint32_t approved = 0;
    uint32_t rejected = 0;
    uint32_t filled = 0;

    while (generator.has_next()) {
        const SyntheticOrder order = generator.next();
        luv::exec::OrderIntent intent{};
        intent.symbol_idx = 3;
        intent.side = static_cast<uint8_t>(order.side);
        intent.qty = static_cast<int64_t>(order.qty);
        intent.price = order.price;
        intent.alpha_timestamp_ns = 0;
        intent.now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        intent.client_order_id = order.client_id;

        luv::OutboundPacket packet{};
        const auto decision = gateway.try_build(intent, packet);
        collector.record_order_sent();
        collector.record_order_check(decision.pass != 0);
        ++sent;
        if (decision.pass) {
            ++approved;
            collector.record_ack();
            collector.record_fill(1000);
            ++filled;
            sim.process_order(packet);
        } else {
            ++rejected;
        }

        if (sent >= kOrderCount) break;
    }

    collector.set_position(static_cast<int64_t>(approved - filled));
    assert(collector.flush(arena));
    const bool ok = verify_counts_and_replay(gateway, collector, ledger, sent, approved, rejected, filled);
    sim.stop();

    std::printf("staging_runner: final ok=%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
