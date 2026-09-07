#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "luv_execution.hpp"
#include "luv_recovery.hpp"

namespace {

constexpr const char* kLedgerPath = "/tmp/luv_crash_recovery_harness.bin";
constexpr uint32_t kScenarioCount = 3;

struct CrashScenario {
    const char* name = "";
    uint32_t crash_after_order = 0;
};

struct ReplaySummary {
    uint64_t total_events = 0;
    uint32_t active_orders = 0;
    int64_t net_position = 0;
};

void reset_ledger() {
    ::unlink(kLedgerPath);
}

static uint64_t monotonic_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

void force_crash() {
    std::fflush(stdout);
    std::fflush(stderr);
    ::kill(::getpid(), SIGKILL);
    std::abort();
}

bool build_and_crash_child(uint32_t crash_after_order) {
    reset_ledger();

    luv::Arena arena;
    if (!arena.init()) {
        std::fprintf(stderr, "arena init failed\n");
        return false;
    }

    luv::ExecutionGateway gateway;
    if (!gateway.init(arena)) {
        std::fprintf(stderr, "gateway init failed\n");
        return false;
    }

    luv::RecoveryLedger ledger;
    if (!ledger.open(kLedgerPath)) {
        std::fprintf(stderr, "ledger open failed\n");
        return false;
    }
    gateway.set_recovery_ledger(&ledger);

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);

    for (uint32_t i = 1; i <= crash_after_order; ++i) {
        const uint64_t now_ns = monotonic_ns();
        luv::exec::OrderIntent intent{};
        intent.symbol_idx = 3;
        intent.side = (i % 2 == 0) ? luv::exec::kSell : luv::exec::kBuy;
        intent.qty = 100 + static_cast<int64_t>((i % 10) * 10);
        intent.price = 1'000'000 + static_cast<int64_t>(i * 1000);
        intent.alpha_timestamp_ns = now_ns - 50'000ULL;
        intent.now_ns = now_ns;
        intent.client_order_id = 1000u + i;

        luv::OutboundPacket packet{};
        const auto decision = gateway.try_build(intent, packet);
        if (!decision.pass) {
            std::fprintf(stderr, "child rejected order %u (mask=%u)\n", i, decision.reject_mask);
            continue;
        }

        if (i == crash_after_order) {
            force_crash();
        }
    }

    return true;
}

ReplaySummary replay_state() {
    ReplaySummary summary{};
    luv::RecoveryLedger ledger;
    if (!ledger.open(kLedgerPath)) {
        std::fprintf(stderr, "replay open failed\n");
        return summary;
    }

    std::vector<luv::RecoveredOrder> orders(256);
    uint32_t count = 0;
    if (!ledger.replay(orders.data(), static_cast<uint32_t>(orders.size()), count)) {
        std::fprintf(stderr, "replay failed\n");
        return summary;
    }

    summary.total_events = ledger.size();
    summary.active_orders = count;
    for (uint32_t i = 0; i < count; ++i) {
        summary.net_position += orders[i].remaining;
    }
    return summary;
}

bool run_scenario(const CrashScenario& scenario) {
    reset_ledger();
    const pid_t child = ::fork();
    if (child == -1) {
        std::perror("fork");
        return false;
    }
    if (child == 0) {
        const bool ok = build_and_crash_child(scenario.crash_after_order);
        std::exit(ok ? 0 : 1);
    }

    int status = 0;
    const pid_t waited = ::waitpid(child, &status, 0);
    if (waited == -1) {
        std::perror("waitpid");
        return false;
    }

    const bool child_was_killed = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
    if (!child_was_killed) {
        std::fprintf(stderr, "%s: child did not crash as expected\n", scenario.name);
        return false;
    }

    const ReplaySummary replayed = replay_state();
    const bool pass = replayed.total_events >= scenario.crash_after_order &&
                      replayed.active_orders >= 1;

    std::printf("%s: crashed=%s replay_events=%llu active=%u net_position=%lld\n",
                scenario.name,
                child_was_killed ? "YES" : "NO",
                static_cast<unsigned long long>(replayed.total_events),
                replayed.active_orders,
                static_cast<long long>(replayed.net_position));
    return pass;
}

}  // namespace

int main() {
    const CrashScenario scenarios[kScenarioCount] = {
        {"Crash after order 10", 10},
        {"Crash after order 25", 25},
        {"Crash after order 50", 50},
    };

    bool all_ok = true;
    for (const auto& scenario : scenarios) {
        const bool ok = run_scenario(scenario);
        std::printf("SCENARIO %s: %s\n", scenario.name, ok ? "PASS" : "FAIL");
        all_ok = all_ok && ok;
    }

    return all_ok ? 0 : 1;
}
