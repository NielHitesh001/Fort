#include "luv_rule_605.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_sec_rule_605_order_execution_quality() {
    luv::compliance::Rule605Reporter reporter;

    // Order 1: Market Order Buy 500 shares, Midpoint $100.00 (1000000), Fill $99.98 (999800) -> Price Improvement of 2 cents
    assert(reporter.record_order(luv::compliance::Rule605OrderEntry{
        .order_id = 1,
        .order_type = luv::compliance::Rule605OrderType::kMarket,
        .side = luv::exec::kBuy,
        .order_qty = 500,
        .executed_qty = 500,
        .quote_midpoint = 100'0000LL,
        .fill_price = 99'9800LL,
        .exec_latency_ns = 500'000 // 0.5ms
    }));

    // Order 2: Marketable Limit Sell 300 shares, Midpoint $100.00 (1000000), Fill $100.00 (1000000) -> At Quote
    assert(reporter.record_order(luv::compliance::Rule605OrderEntry{
        .order_id = 2,
        .order_type = luv::compliance::Rule605OrderType::kMarketableLimit,
        .side = luv::exec::kSell,
        .order_qty = 300,
        .executed_qty = 300,
        .quote_midpoint = 100'0000LL,
        .fill_price = 100'0000LL,
        .exec_latency_ns = 1'500'000 // 1.5ms
    }));

    auto report = reporter.generate_monthly_report();

    assert(report.total_covered_orders == 2);
    assert(report.total_shares_executed == 800);
    assert(report.total_price_improved_shares == 500);
    assert(report.total_at_quote_shares == 300);
    assert(std::fabs(report.avg_execution_time_ms - 1.0) < 1e-4);

    std::printf("[PASS] test_sec_rule_605_order_execution_quality (Orders: %lld, Price Improved: %lld shares, Avg Time: %.2fms)\n",
        static_cast<long long>(report.total_covered_orders),
        static_cast<long long>(report.total_price_improved_shares),
        report.avg_execution_time_ms);
}

int main() {
    test_sec_rule_605_order_execution_quality();
    std::printf("All SEC Rule 605 order execution quality tests passed successfully.\n");
    return 0;
}
