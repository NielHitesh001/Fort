#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <mach/mach.h>
#include <thread>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_recovery.hpp"
#include "luv_telemetry.hpp"

namespace {

constexpr const char* kLedgerPath = "/tmp/luv_soak_ledger.bin";

struct SoakConfig {
    uint32_t target_rate_per_sec = 100;
    uint64_t soak_duration_seconds = 3600;
    const char* ledger_path = kLedgerPath;
};

static uint64_t monotonic_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

static bool parse_cli(int argc, char** argv, SoakConfig& cfg) noexcept {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            cfg.soak_duration_seconds = static_cast<uint64_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            cfg.target_rate_per_sec = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--ledger") == 0 && i + 1 < argc) {
            cfg.ledger_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("Usage: %s [--duration seconds] [--rate per_sec] [--ledger path]\n", argv[0]);
            return false;
        }
    }
    return true;
}

static size_t current_resident_bytes() noexcept {
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t status = task_info(
        mach_task_self(),
        MACH_TASK_BASIC_INFO,
        reinterpret_cast<task_info_t>(&info),
        &count);
    return (status == KERN_SUCCESS) ? static_cast<size_t>(info.resident_size) : 0u;
}

}  // namespace

int main(int argc, char** argv) {
    SoakConfig config{};
    if (!parse_cli(argc, argv, config)) {
        return 0;
    }

    ::unlink(config.ledger_path);

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
    if (!ledger.open(config.ledger_path)) {
        std::fprintf(stderr, "ledger open failed\n");
        return 1;
    }
    gateway.set_recovery_ledger(&ledger);

    luv::TelemetryBatchCollector<256> telemetry;

    const size_t initial_rss = current_resident_bytes();

    const auto start = std::chrono::steady_clock::now();
    const auto end = start + std::chrono::seconds(static_cast<int64_t>(config.soak_duration_seconds));

    uint64_t sent = 0;
    uint64_t approved = 0;
    uint64_t rejected = 0;
    uint64_t filled = 0;

    std::printf("Starting soak test: %.1f hours at %u orders/sec\n",
                static_cast<double>(config.soak_duration_seconds) / 3600.0,
                config.target_rate_per_sec);
    std::fflush(stdout);

    while (std::chrono::steady_clock::now() < end) {
        const uint64_t now_ns = monotonic_ns();
        const uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count());
        const uint64_t expected = elapsed * config.target_rate_per_sec;

        if (sent >= expected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        luv::exec::OrderIntent intent{};
        intent.symbol_idx = 3;
        intent.side = (sent % 2 == 0) ? luv::exec::kBuy : luv::exec::kSell;
        intent.qty = 100 + static_cast<int64_t>(sent % 50);
        intent.price = 1'000'000 + static_cast<int64_t>((sent % 250) * 1000);
        intent.alpha_timestamp_ns = now_ns - 50'000ULL;
        intent.now_ns = now_ns;
        intent.client_order_id = 2000u + static_cast<uint32_t>(sent);

        luv::OutboundPacket packet{};
        const auto decision = gateway.try_build(intent, packet);
        telemetry.record_order_sent();
        ++sent;

        if (decision.pass) {
            ++approved;
            telemetry.record_order_check(true);
            telemetry.record_ack();
            telemetry.record_fill(static_cast<int64_t>(intent.qty));
            ++filled;
        } else {
            ++rejected;
            telemetry.record_order_check(false);
        }

        if (sent % 10000 == 0) {
            const auto snapshot = telemetry.batch();
            const bool drift = snapshot.orders_sent != sent ||
                               snapshot.orders_approved != approved ||
                               snapshot.orders_rejected != rejected;
            std::printf("[%llu s] Orders: sent=%llu approved=%llu rejected=%llu filled=%llu drift=%s\n",
                        static_cast<unsigned long long>(elapsed),
                        static_cast<unsigned long long>(sent),
                        static_cast<unsigned long long>(approved),
                        static_cast<unsigned long long>(rejected),
                        static_cast<unsigned long long>(filled),
                        drift ? "FAIL" : "PASS");
            std::fflush(stdout);
            if (drift) {
                std::fprintf(stderr, "ERROR: telemetry drift at %llu orders\n",
                            static_cast<unsigned long long>(sent));
                return 1;
            }
        }
    }

    const size_t final_rss = current_resident_bytes();
    const long rss_delta = static_cast<long>(final_rss) - static_cast<long>(initial_rss);
    const double rss_growth_pct = (initial_rss == 0) ? 0.0 :
        (static_cast<double>(rss_delta) * 100.0 / static_cast<double>(initial_rss));

    const auto snapshot = telemetry.batch();
    const bool reconcile_ok = (sent == approved + rejected) &&
                             (filled <= approved) &&
                             (snapshot.orders_sent == sent) &&
                             (snapshot.orders_approved == approved) &&
                             (snapshot.orders_rejected == rejected);
    const bool memory_ok = rss_growth_pct < 10.0;

    std::printf("\nSoak complete. Final state:\n");
    std::printf("  Orders sent: %llu\n", static_cast<unsigned long long>(sent));
    std::printf("  Orders approved: %llu\n", static_cast<unsigned long long>(approved));
    std::printf("  Orders rejected: %llu\n", static_cast<unsigned long long>(rejected));
    std::printf("  Orders filled: %llu\n", static_cast<unsigned long long>(filled));
    std::printf("  Memory check: RSS %zu -> %zu (delta=%ld, %.1f%%)\n",
                initial_rss, final_rss, rss_delta, rss_growth_pct);
    std::printf("  Telemetry drift check: %s\n", reconcile_ok ? "PASS" : "FAIL");
    std::printf("  Reconciliation: %s\n", reconcile_ok ? "PASS" : "FAIL");
    std::printf("  Soak test result: %s\n", (reconcile_ok && memory_ok) ? "PASS" : "FAIL");
    std::fflush(stdout);

    return (reconcile_ok && memory_ok) ? 0 : 1;
}
