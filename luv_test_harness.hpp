#pragma once

// Test-only failure injection around actual simulator components. This is not
// a live-feed reconnect implementation or an executable recovery controller.
#include "luv_execution.hpp"
#include "luv_feed_sim.hpp"
#include <cassert>
#include <cstdlib>

namespace luv::test {
struct FailureHarness {
    Arena arena;
    ExecutionGateway gateway;
    RecoveryLedger ledger;
    SimFeedSource feed{SimConfig{.synthetic_symbols = 1, .target_rate_hz = 0,
        .prebuf_count = 16}};
    char ledger_path[64] = "/tmp/fort-failure-XXXXXX";
    bool feed_connected = true;

    FailureHarness() {
        const int fd = ::mkstemp(ledger_path);
        assert(fd >= 0);
        ::close(fd);
        assert(arena.init());
        assert(gateway.init(arena));
        assert(feed.init(arena));
        assert(ledger.open(ledger_path));
        gateway.set_recovery_ledger(&ledger);
        exec::RiskLimits limits{};
        limits.max_order_qty = 1000;
        limits.max_abs_position = 1000000;
        limits.max_alpha_age_ns = 1000000;
        assert(gateway.risk().set_limits(0, limits));
    }
    ~FailureHarness() {
        gateway.set_recovery_ledger(nullptr);
        ledger.close();
        ::unlink(ledger_path);
    }
    bool submit(uint32_t id, int64_t qty = 100) {
        exec::OrderIntent intent{};
        intent.symbol_idx = 0;
        intent.side = exec::kBuy;
        intent.qty = qty;
        intent.price = 1000000;
        intent.alpha_timestamp_ns = 1;
        intent.now_ns = 1;
        intent.client_order_id = id;
        OutboundPacket packet{};
        return gateway.try_build(intent, packet).pass != 0;
    }
    uint32_t poll_feed() { return feed_connected ? feed.poll() : 0; }
};
} // namespace luv::test
