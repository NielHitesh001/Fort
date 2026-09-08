#include "luv_perps_funding.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_crypto_perpetual_funding_rate() {
    luv::crypto::PerpetualFundingEngine engine;

    // BTC Mark price consistently trading at premium above Index price:
    // Mark = $50,100 (501000000), Index = $50,000 (500000000) -> Premium = +0.20% (+0.0020)
    for (uint64_t t = 1; t <= 10; ++t) {
        assert(engine.record_price_sample(t * 60'000'000'000ULL, 501'000'0000LL, 500'000'0000LL));
    }

    double funding_rate = engine.compute_funding_rate();
    // Expected funding rate: approx +0.0020 (clamped within +/-0.0075)
    assert(funding_rate > 0.0010 && funding_rate <= 0.0075);

    // Compute funding payment for Long 2 BTC position:
    // Long pays Short because funding_rate > 0
    int64_t payment = engine.compute_funding_payment(2, 501'000'0000LL, funding_rate);
    assert(payment > 0);

    std::printf("[PASS] test_crypto_perpetual_funding_rate (8h Funding Rate: %.4f%%, Long 2 BTC Payment: $%.2f)\n",
        funding_rate * 100.0, static_cast<double>(payment) / 10000.0);
}

int main() {
    test_crypto_perpetual_funding_rate();
    std::printf("All crypto perpetual funding rate tests passed successfully.\n");
    return 0;
}
