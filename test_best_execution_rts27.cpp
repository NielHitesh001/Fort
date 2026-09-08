#include "luv_best_execution_rts27.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_mifid_rts27_execution_quality() {
    luv::best_ex::BestExecutionRts27Engine engine;

    // Order 1: Buy 100 shares @ $100.00 (arrival midpoint $100.02) -> Price improvement of 2 cents
    assert(engine.record_execution(luv::best_ex::ExecutionQualityRecord{
        .order_id = 1,
        .symbol_idx = 1,
        .side = luv::exec::kBuy,
        .arrival_midpoint = 1000200,
        .fill_price = 1000000,
        .fill_qty = 100,
        .latency_ns = 250 // 250ns
    }));

    // Order 2: Sell 200 shares @ $100.05 (arrival midpoint $100.03) -> Price improvement of 2 cents
    assert(engine.record_execution(luv::best_ex::ExecutionQualityRecord{
        .order_id = 2,
        .symbol_idx = 1,
        .side = luv::exec::kSell,
        .arrival_midpoint = 1000300,
        .fill_price = 1000500,
        .fill_qty = 200,
        .latency_ns = 450 // 450ns
    }));

    // Order 3: Buy 500 shares @ $100.04 (arrival midpoint $100.00) -> Passive crossing spread
    assert(engine.record_execution(luv::best_ex::ExecutionQualityRecord{
        .order_id = 3,
        .symbol_idx = 1,
        .side = luv::exec::kBuy,
        .arrival_midpoint = 1000000,
        .fill_price = 1000400,
        .fill_qty = 500,
        .latency_ns = 800 // 800ns
    }));

    auto report = engine.generate_rts27_report();

    assert(report.total_orders == 3);
    assert(report.total_executed_qty == 800);
    assert(report.total_price_improved_orders == 2);
    assert(report.total_price_improved_qty == 300);
    assert(report.p50_latency_ns == 450);

    std::printf("[PASS] test_mifid_rts27_execution_quality (Orders: %lld, Price Improved: %lld, P50 Latency: %lluns)\n",
        static_cast<long long>(report.total_orders),
        static_cast<long long>(report.total_price_improved_orders),
        static_cast<unsigned long long>(report.p50_latency_ns));
}

int main() {
    test_mifid_rts27_execution_quality();
    std::printf("All MiFID II RTS 27 Execution Quality tests passed successfully.\n");
    return 0;
}
