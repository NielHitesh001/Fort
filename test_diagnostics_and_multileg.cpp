#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "luv_diagnostics.hpp"
#include "luv_multileg.hpp"

using namespace luv;

void test_crash_diagnostics() {
    std::printf("[test_crash_diagnostics] Running...\n");
    const char* dump_file = "/tmp/test_crash_dump.json";
    ::unlink(dump_file);

    CrashDiagnosticsHandler::set_dump_path(dump_file);
    CrashDiagnosticsHandler::set_state_context(123456, 42, true);

    // Call write_crash_dump directly (without fatal signal invocation)
    CrashDiagnosticsHandler::write_crash_dump(SIGSEGV, nullptr);

    int fd = ::open(dump_file, O_RDONLY);
    assert(fd >= 0);
    char buf[512]{};
    const ssize_t bytes = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    assert(bytes > 0);

    assert(std::strstr(buf, "\"event\": \"CRASH_DIAGNOSTICS\"") != nullptr);
    assert(std::strstr(buf, "\"last_sequence\": 123456") != nullptr);
    assert(std::strstr(buf, "\"active_orders\": 42") != nullptr);
    assert(std::strstr(buf, "\"circuit_breaker_tripped\": true") != nullptr);

    ::unlink(dump_file);
    std::printf("[test_crash_diagnostics] PASSED\n");
}

void test_multileg_spread_execution() {
    std::printf("[test_multileg_spread_execution] Running...\n");

    SpreadOrder spread{};
    spread.spread_id = 101;
    spread.leg1_symbol = 0; // Buy Stock A
    spread.leg1_side = exec::kBuy;
    spread.leg1_qty = 100;
    spread.leg1_limit_price = 150 * 10'000;

    spread.leg2_symbol = 1; // Sell Stock B
    spread.leg2_side = exec::kSell;
    spread.leg2_ratio = 2; // 1:2 ratio
    spread.leg2_limit_price = 75 * 10'000;
    spread.legging_timeout_ns = 20'000'000; // 20ms

    uint64_t now_ns = 1'000'000'000ULL;

    // 1. Initial Leg 1 Intent
    auto leg1_intent = MultiLegSpreadEngine::generate_leg1_intent(spread, now_ns);
    assert(leg1_intent.symbol_idx == 0);
    assert(leg1_intent.side == exec::kBuy);
    assert(leg1_intent.qty == 100);
    assert(leg1_intent.price == 150 * 10'000);

    // 2. Leg 1 Fills -> Generates Leg 2 Hedge Intent (Qty = 100 * 2 = 200)
    auto leg2_intent = MultiLegSpreadEngine::on_leg1_fill(spread, 100, now_ns + 1'000'000);
    assert(spread.state == SpreadLegState::kLeg1Filled);
    assert(leg2_intent.symbol_idx == 1);
    assert(leg2_intent.side == exec::kSell);
    assert(leg2_intent.qty == 200);
    assert(leg2_intent.price == 75 * 10'000);

    // 3. Legging Timeout Detection
    assert(!MultiLegSpreadEngine::check_legging_timeout(spread, now_ns + 10'000'000)); // 10ms < 20ms
    assert(MultiLegSpreadEngine::check_legging_timeout(spread, now_ns + 30'000'000));  // 30ms > 20ms

    // 4. Emergency Leg 1 Unwind Intent
    auto unwind_intent = MultiLegSpreadEngine::generate_emergency_unwind(spread, now_ns + 30'000'000);
    assert(spread.state == SpreadLegState::kLeggingUnwind);
    assert(unwind_intent.symbol_idx == 0);
    assert(unwind_intent.side == exec::kSell); // Inverted to close Long Leg 1
    assert(unwind_intent.qty == 100);

    std::printf("[test_multileg_spread_execution] PASSED\n");
}

int main() {
    test_crash_diagnostics();
    test_multileg_spread_execution();
    std::printf("ALL DIAGNOSTICS & MULTI-LEG TESTS PASSED\n");
    return 0;
}
