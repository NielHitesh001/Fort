#include "luv_collar.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

void test_adaptive_collar_bounds() {
    luv::AdaptiveCollarConfig config;
    config.min_collar_bps = 100.0; // 1.00%
    config.max_collar_bps = 500.0; // 5.00%
    config.volatility_multiplier = 2.0;

    luv::AdaptiveVolatilityCollar collar(config);

    // Initial trades around 10000 with low dispersion
    for (int i = 0; i < 50; ++i) {
        collar.on_trade(10000 + (i % 5));
    }

    int64_t lower = 0;
    int64_t upper = 0;
    double collar_bps = 0.0;
    assert(collar.compute_collar_bounds(10000, lower, upper, &collar_bps));
    assert(collar_bps == 100.0); // Clamped to min 100 bps
    assert(lower == 9900);
    assert(upper == 10100);

    // Valid price
    assert(collar.validate_price(10050, 10000, luv::exec::kBuy));
    assert(collar.validate_price(9950, 10000, luv::exec::kSell));

    // Outside collar
    assert(!collar.validate_price(10150, 10000, luv::exec::kBuy)); // Buy too high
    assert(!collar.validate_price(9800, 10000, luv::exec::kSell)); // Sell too low

    std::printf("[PASS] test_adaptive_collar_bounds\n");
}

void test_volatility_expansion() {
    luv::AdaptiveCollarConfig config;
    config.min_collar_bps = 50.0;
    config.max_collar_bps = 1000.0;
    config.volatility_multiplier = 3.0;
    config.ema_alpha = 0.2;

    luv::AdaptiveVolatilityCollar collar(config);

    // Introduce large price swings (high volatility)
    for (int i = 0; i < 50; ++i) {
        collar.on_trade((i % 2 == 0) ? 10500 : 9500);
    }

    int64_t lower = 0;
    int64_t upper = 0;
    double collar_bps = 0.0;
    assert(collar.compute_collar_bounds(10000, lower, upper, &collar_bps));
    assert(collar_bps > 200.0); // Expanded significantly beyond minimum

    std::printf("[PASS] test_volatility_expansion (Collar expanded to %.2f bps)\n", collar_bps);
}

int main() {
    test_adaptive_collar_bounds();
    test_volatility_expansion();
    std::printf("All adaptive volatility collar tests passed successfully.\n");
    return 0;
}
