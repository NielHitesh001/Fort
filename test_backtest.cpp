#include "luv_backtest.hpp"
#include <cassert>
#include <cstdio>

void test_backtest_simulation_engine() {
    // 500ns simulated transit latency
    luv::backtest::BacktestEngine engine(500);

    // Feed market events (timestamps 1000ns, 2000ns, 3000ns)
    // T=1000: Bid 100 @ 10, Ask 102 @ 10
    engine.load_event(luv::backtest::MarketEvent{
        .timestamp_ns = 1000,
        .symbol_idx = 1,
        .best_bid = 100,
        .best_ask = 102,
        .bid_sz = 10,
        .ask_sz = 10
    });

    // T=2000: Bid 101 @ 10, Ask 103 @ 10
    engine.load_event(luv::backtest::MarketEvent{
        .timestamp_ns = 2000,
        .symbol_idx = 1,
        .best_bid = 101,
        .best_ask = 103,
        .bid_sz = 10,
        .ask_sz = 10
    });

    // T=3000: Bid 105 @ 10, Ask 107 @ 10
    engine.load_event(luv::backtest::MarketEvent{
        .timestamp_ns = 3000,
        .symbol_idx = 1,
        .best_bid = 105,
        .best_ask = 107,
        .bid_sz = 10,
        .ask_sz = 10
    });

    // Order 1: Buy 5 @ 102 at T=400 (arrives T=900 < 1000) -> Matches Ask 102 at T=1000
    assert(engine.submit_order(1, 1, luv::exec::kBuy, 102, 5, 400));

    // Order 2: Sell 5 @ 105 at T=2000 (arrives T=2500 < 3000) -> Matches Bid 105 at T=3000
    assert(engine.submit_order(2, 1, luv::exec::kSell, 105, 5, 2000));

    engine.run_simulation();

    auto metrics = engine.compute_metrics();
    assert(metrics.total_trades == 2);
    // PnL: Buy 5 @ 102 (-510), Sell 5 @ 105 (+525) = Net PnL +15
    assert(metrics.total_pnl == 15);
    assert(metrics.win_rate == 1.0);

    std::printf("[PASS] test_backtest_simulation_engine (PnL: %lld, Trades: %lld)\n",
        static_cast<long long>(metrics.total_pnl),
        static_cast<long long>(metrics.total_trades));
}

int main() {
    test_backtest_simulation_engine();
    std::printf("All backtesting simulation tests passed successfully.\n");
    return 0;
}
