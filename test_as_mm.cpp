#include "luv_as_mm.hpp"
#include <cassert>
#include <cstdio>

void test_avellaneda_stoikov_inventory_skew() {
    luv::mm::AvellanedaStoikovConfig config;
    config.gamma = 0.1;
    config.sigma = 2.0; // 2 price points volatility
    config.kappa = 1.5;
    config.time_horizon = 1.0;
    config.tick_size = 1;

    luv::mm::AvellanedaStoikovQuoter quoter(config);
    int64_t mid = 10000;

    // 1. Zero inventory: symmetric reservation price = mid
    auto q_neutral = quoter.compute_quotes(mid, 0);
    assert(q_neutral.reservation_price == mid);
    assert(q_neutral.bid_price < mid);
    assert(q_neutral.ask_price > mid);

    // 2. Long inventory (q = +100): reservation price drops below mid to encourage selling
    auto q_long = quoter.compute_quotes(mid, 100);
    assert(q_long.reservation_price < mid);
    assert(q_long.bid_price < q_neutral.bid_price); // More defensive bid
    assert(q_long.ask_price < q_neutral.ask_price); // More aggressive ask

    // 3. Short inventory (q = -100): reservation price rises above mid to encourage buying
    auto q_short = quoter.compute_quotes(mid, -100);
    assert(q_short.reservation_price > mid);
    assert(q_short.bid_price > q_neutral.bid_price); // More aggressive bid
    assert(q_short.ask_price > q_neutral.ask_price); // More defensive ask

    std::printf("[PASS] test_avellaneda_stoikov_inventory_skew (Neutral: %lld/%lld, Long: %lld/%lld, Short: %lld/%lld)\n",
        static_cast<long long>(q_neutral.bid_price), static_cast<long long>(q_neutral.ask_price),
        static_cast<long long>(q_long.bid_price), static_cast<long long>(q_long.ask_price),
        static_cast<long long>(q_short.bid_price), static_cast<long long>(q_short.ask_price));
}

int main() {
    test_avellaneda_stoikov_inventory_skew();
    std::printf("All Avellaneda-Stoikov market making tests passed successfully.\n");
    return 0;
}
