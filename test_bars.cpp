#include "luv_bars.hpp"
#include <cassert>
#include <cstdio>

void test_ohlcv_bar_aggregation() {
    // 60-second time aggregator
    luv::bars::TimeBarAggregator<60> aggregator;
    uint16_t sym = 1;

    luv::bars::OhlcvBar completed_bar;

    // Minute 0 trades:
    // Tick 1 (t = 10s): Price 100, Qty 100
    assert(!aggregator.on_trade(sym, 100, 100, 10'000'000'000ULL, completed_bar));
    // Tick 2 (t = 20s): Price 105, Qty 200 (High = 105)
    assert(!aggregator.on_trade(sym, 105, 200, 20'000'000'000ULL, completed_bar));
    // Tick 3 (t = 30s): Price 95, Qty 100 (Low = 95)
    assert(!aggregator.on_trade(sym, 95, 100, 30'000'000'000ULL, completed_bar));
    // Tick 4 (t = 40s): Price 102, Qty 100 (Close = 102)
    assert(!aggregator.on_trade(sym, 102, 100, 40'000'000'000ULL, completed_bar));

    // Minute 1 trade:
    // Tick 5 (t = 70s): Triggers roll of Minute 0 bar!
    bool rolled = aggregator.on_trade(sym, 103, 50, 70'000'000'000ULL, completed_bar);
    assert(rolled);
    assert(completed_bar.completed);
    assert(completed_bar.open_price == 100);
    assert(completed_bar.high_price == 105);
    assert(completed_bar.low_price == 95);
    assert(completed_bar.close_price == 102);
    assert(completed_bar.volume == 500); // 100 + 200 + 100 + 100 = 500
    assert(completed_bar.trade_count == 4);

    // VWAP = (100*100 + 105*200 + 95*100 + 102*100) / 500 = (10000 + 21000 + 9500 + 10200) / 500 = 50700 / 500 = 101.4 -> 101
    assert(completed_bar.vwap() == 101);

    std::printf("[PASS] test_ohlcv_bar_aggregation (O: %lld, H: %lld, L: %lld, C: %lld, Vol: %lld, VWAP: %lld)\n",
        static_cast<long long>(completed_bar.open_price),
        static_cast<long long>(completed_bar.high_price),
        static_cast<long long>(completed_bar.low_price),
        static_cast<long long>(completed_bar.close_price),
        static_cast<long long>(completed_bar.volume),
        static_cast<long long>(completed_bar.vwap()));
}

int main() {
    test_ohlcv_bar_aggregation();
    std::printf("All OHLCV bar aggregation tests passed successfully.\n");
    return 0;
}
