#include "luv_fx.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_synthetic_cross_computation() {
    // EUR/USD: 1.08500 / 1.08520 (5 decimals)
    luv::fx::FxQuote eurusd{ .bid_price = 108500, .ask_price = 108520, .decimals = 5 };
    // USD/JPY: 155.200 / 155.250 (3 decimals)
    luv::fx::FxQuote usdjpy{ .bid_price = 155200, .ask_price = 155250, .decimals = 3 };

    // Synthetic EUR/JPY (3 decimals)
    // Synthetic Bid = 1.08500 * 155.200 = 168.392
    // Synthetic Ask = 1.08520 * 155.250 = 168.477
    auto eurjpy = luv::fx::FxTriangulationEngine::compute_synthetic_cross(eurusd, usdjpy, 3);
    assert(eurjpy.bid_price == 168392);
    assert(eurjpy.ask_price == 168477);

    std::printf("[PASS] test_synthetic_cross_computation (EUR/JPY: %.3f / %.3f)\n",
        eurjpy.bid_price / 1000.0, eurjpy.ask_price / 1000.0);
}

void test_triangular_arbitrage_detection() {
    // EUR/USD: 1.08500 / 1.08520
    luv::fx::FxQuote eurusd{ .bid_price = 108500, .ask_price = 108520, .decimals = 5 };
    // USD/JPY: 155.200 / 155.250
    luv::fx::FxQuote usdjpy{ .bid_price = 155200, .ask_price = 155250, .decimals = 3 };

    // Direct EUR/JPY market quote is artificially high at 168.600 / 168.650
    // Direct Bid (168.600) > Synthetic Ask (168.477)
    // Profit = (168.600 - 168.477) / 168.600 = ~7.29 bps > 2 bps threshold!
    luv::fx::FxQuote direct_high{ .bid_price = 168600, .ask_price = 168650, .decimals = 3 };

    auto arb = luv::fx::FxTriangulationEngine::evaluate_arbitrage(direct_high, eurusd, usdjpy, 2.0);
    assert(arb.detected);
    assert(arb.buy_synthetic);
    assert(arb.profit_bps > 5.0);

    std::printf("[PASS] test_triangular_arbitrage_detection (Profit: %.2f bps)\n", arb.profit_bps);
}

int main() {
    test_synthetic_cross_computation();
    test_triangular_arbitrage_detection();
    std::printf("All FX triangulation & arbitrage tests passed successfully.\n");
    return 0;
}
