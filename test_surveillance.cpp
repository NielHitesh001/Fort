#include "luv_surveillance.hpp"
#include <cassert>
#include <cstdio>

void test_quote_stuffing_detection() {
    luv::surveillance::SurveillanceConfig config;
    config.burst_window_ns = 10'000'000; // 10ms
    config.max_burst_messages = 50;      // 50 msgs limit

    luv::surveillance::MarketSurveillanceEngine engine(config);
    uint32_t bad_actor = 999;

    // Send 40 messages within 5ms -> No alert
    for (uint32_t i = 0; i < 40; ++i) {
        auto alert = engine.observe_event(bad_actor, 1, luv::exec::kBuy, 1, 100, i * 100'000);
        assert(alert == luv::surveillance::SurveillanceAlert::kNone);
    }

    // Send another 20 messages within 2ms -> Exceeds 50 msgs limit -> Quote Stuffing Alert!
    luv::surveillance::SurveillanceAlert final_alert = luv::surveillance::SurveillanceAlert::kNone;
    for (uint32_t i = 40; i < 60; ++i) {
        final_alert = engine.observe_event(bad_actor, 1, luv::exec::kBuy, 1, 100, 4'000'000 + i * 10'000);
    }
    assert(final_alert == luv::surveillance::SurveillanceAlert::kQuoteStuffing);

    std::printf("[PASS] test_quote_stuffing_detection\n");
}

void test_spoofing_layering_detection() {
    luv::surveillance::SurveillanceConfig config;
    config.min_spoofing_samples = 5;
    config.spoofing_cancel_ratio = 0.80;

    luv::surveillance::MarketSurveillanceEngine engine(config);
    uint32_t spoofer = 888;
    uint16_t sym = 1;

    // 1. Spoofer places 10 fake Buy orders (side=Buy, type=1)
    for (uint32_t i = 0; i < 10; ++i) {
        engine.observe_event(spoofer, sym, luv::exec::kBuy, 1, 1000, i * 1000);
    }

    // 2. Spoofer cancels 9 of those Buy orders (side=Buy, type=2) -> 90% cancel ratio
    for (uint32_t i = 0; i < 9; ++i) {
        engine.observe_event(spoofer, sym, luv::exec::kBuy, 2, 1000, 10000 + i * 1000);
    }

    // 3. Spoofer executes real Sell order on opposite side (side=Sell, type=3)
    auto alert = engine.observe_event(spoofer, sym, luv::exec::kSell, 3, 500, 20000);
    assert(alert == luv::surveillance::SurveillanceAlert::kSpoofingLayering);

    std::printf("[PASS] test_spoofing_layering_detection\n");
}

int main() {
    test_quote_stuffing_detection();
    test_spoofing_layering_detection();
    std::printf("All market surveillance & spoofing detection tests passed successfully.\n");
    return 0;
}
