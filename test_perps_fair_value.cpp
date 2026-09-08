#include "luv_perps_fair_value.hpp"
#include <cassert>
#include <cstdio>

void test_crypto_fair_mark_price_engine() {
    luv::crypto::FairMarkPriceEngine engine;

    // Register 4 major spot exchanges (Binance, Coinbase, Kraken, OKX) with equal 25% weight
    assert(engine.add_spot_venue(1, 0.25));
    assert(engine.add_spot_venue(2, 0.25));
    assert(engine.add_spot_venue(3, 0.25));
    assert(engine.add_spot_venue(4, 0.25));

    // Update spot prices around $50,000 (500000000)
    assert(engine.update_spot_price(1, 500'010'0000LL));
    assert(engine.update_spot_price(2, 499'990'0000LL));
    assert(engine.update_spot_price(3, 500'000'0000LL));
    assert(engine.update_spot_price(4, 500'000'0000LL));

    int64_t index_price = engine.compute_index_price();
    assert(index_price == 500'000'0000LL); // Exactly $50,000.00

    // Test 1: Orderbook mid matches index price exactly -> Fair mark = $50,000.00
    int64_t mark_price_1 = engine.compute_fair_mark_price(500'000'0000LL);
    assert(mark_price_1 == 500'000'0000LL);

    // Test 2: Orderbook mid experiences flash-loan manipulation skew ($55,000.00)
    // Fair mark price clamps basis within $50 limit -> Fair mark <= $50,050.00
    int64_t mark_price_manipulated = engine.compute_fair_mark_price(550'000'0000LL);
    assert(mark_price_manipulated <= 500'050'0000LL);

    std::printf("[PASS] test_crypto_fair_mark_price_engine (Index: $50,000.00, Protected Mark: $%lld.%04lld)\n",
        static_cast<long long>(mark_price_manipulated / 10000),
        static_cast<long long>(mark_price_manipulated % 10000));
}

int main() {
    test_crypto_fair_mark_price_engine();
    std::printf("All crypto fair mark price tests passed successfully.\n");
    return 0;
}
