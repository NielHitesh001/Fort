#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_recovery.hpp"
#include "luv_safety.hpp"
#include "luv_telemetry.hpp"

namespace {

constexpr const char* kLedgerPath = "/tmp/luv_main_engine_ledger.bin";

uint64_t monotonic_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

}  // namespace

int main(int argc, char** argv) {
    bool metrics_mode = false;
    const char* ledger_path = kLedgerPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--metrics") == 0) {
            metrics_mode = true;
        } else {
            ledger_path = argv[i];
        }
    }
    ::unlink(ledger_path);

    luv::Arena arena;
    if (!arena.init()) {
        std::fprintf(stderr, "arena init failed\n");
        return 1;
    }

    luv::ExecutionGateway gateway;
    if (!gateway.init(arena)) {
        std::fprintf(stderr, "gateway init failed\n");
        return 1;
    }

    luv::RecoveryLedger ledger;
    if (!ledger.open(ledger_path)) {
        std::fprintf(stderr, "ledger open failed: %s\n", ledger_path);
        return 1;
    }
    gateway.set_recovery_ledger(&ledger);

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 10'000;
    limits.max_abs_position = 100'000;
    limits.max_alpha_age_ns = 250'000;
    gateway.risk().set_limits(0, limits);

    luv::MetricsHttpServer metrics_server;
    if (metrics_mode) {
        luv::TelemSnapshot initial_snapshot{};
        if (!metrics_server.start(initial_snapshot)) {
            std::fprintf(stderr, "metrics server failed to start on port 9090\n");
            return 1;
        }
        std::printf("metrics listening on http://127.0.0.1:%u/metrics\n",
                    metrics_server.port());
    }

    luv::ShutdownController shutdown;
    std::thread trigger;
    if (!metrics_mode) {
        trigger = std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            luv::ShutdownController::request_shutdown();
        });
    }

    luv::TelemetryBatchCollector<256> collector;
    uint64_t order_id = 1;
    const auto start = std::chrono::steady_clock::now();
    while (!shutdown.requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (!metrics_mode && now - start > std::chrono::seconds(1)) break;

        luv::exec::OrderIntent intent{};
        intent.symbol_idx = 0;
        intent.side = (order_id % 2 == 0) ? luv::exec::kBuy : luv::exec::kSell;
        intent.qty = 100 + static_cast<int64_t>(order_id % 50);
        intent.price = 10'000 + static_cast<int64_t>((order_id % 20) * 100);
        intent.alpha_timestamp_ns = monotonic_ns() - 50'000ULL;
        intent.now_ns = monotonic_ns();
        intent.client_order_id = static_cast<uint32_t>(order_id);

        luv::OutboundPacket packet{};
        const auto decision = gateway.try_build(intent, packet);
        collector.record_order_sent();
        collector.record_order_check(decision.pass != 0);

        if (decision.pass) {
            collector.record_ack();
            collector.record_fill(static_cast<int64_t>(intent.qty));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ++order_id;

        if (metrics_mode && luv::TelemetryPublisher::publish_heartbeat(
                arena, 42'000, 3.5f, 99.0f)) {
            if (luv::TelemSnapshot* snapshot = arena.telem_ring.try_peek()) {
                metrics_server.update(*snapshot);
                arena.telem_ring.consume();
            }
        }
    }

    if (trigger.joinable()) trigger.join();
    if (!collector.flush(arena)) {
        std::fprintf(stderr, "telemetry flush failed\n");
        return 1;
    }

    if (metrics_mode) {
        const bool published = luv::TelemetryPublisher::publish_heartbeat(
            arena, 42'000, 3.5f, 99.0f);
        (void)published;
        if (luv::TelemSnapshot* snapshot = arena.telem_ring.try_peek()) {
            metrics_server.update(*snapshot);
            arena.telem_ring.consume();
        }
        while (!shutdown.requested())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("main_engine shutdown=%s orders=%llu\n",
                shutdown.requested() ? "requested" : "normal",
                static_cast<unsigned long long>(order_id - 1));
    return 0;
}
