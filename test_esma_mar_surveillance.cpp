#include "luv_esma_mar_surveillance.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_normal_order_flow() {
    EsmaMarSurveillanceEngine engine;
    uint64_t pid = 1001;
    uint64_t ts = 1'000'000'000ULL;

    // Normal trade
    engine.on_order_new(ts, pid, 1, true, 100'00, 100, 0);
    engine.on_order_fill(ts + 1'000'000ULL, pid, 1, true, 100'00, 100);

    auto alert = engine.evaluate_surveillance(pid, ts + 2'000'000ULL);
    assert(!alert.is_alert_triggered);
    assert(alert.infraction_type == MarInfractionType::None);
}

void test_layering_and_spoofing() {
    EsmaMarSurveillanceEngine engine;
    uint64_t pid = 2002;
    uint64_t ts = 2'000'000'000ULL;

    // Place multi-level passive buy orders to push price up
    engine.on_order_new(ts, pid, 10, true, 99'50, 500, 2);
    engine.on_order_new(ts + 100'000, pid, 11, true, 99'40, 500, 3);
    engine.on_order_new(ts + 200'000, pid, 12, true, 99'30, 500, 4);

    // Enter small sell order at top and get filled
    engine.on_order_new(ts + 300'000, pid, 13, false, 100'00, 50, 0);
    engine.on_order_fill(ts + 400'000, pid, 13, false, 100'00, 50);

    // Immediately cancel all passive buy orders within 1ms
    engine.on_order_cancel(ts + 500'000, pid, 10);
    engine.on_order_cancel(ts + 600'000, pid, 11);
    engine.on_order_cancel(ts + 700'000, pid, 12);

    auto alert = engine.evaluate_surveillance(pid, ts + 800'000);
    assert(alert.is_alert_triggered);
    assert(alert.infraction_type == MarInfractionType::LayeringAndSpoofing);
    assert(alert.confidence_score >= 80);
}

void test_wash_trading() {
    EsmaMarSurveillanceEngine engine;
    uint64_t pid = 3003;
    uint64_t ts = 3'000'000'000ULL;

    // Participant enters buy and sell order with exact matching price & quantity within 10ms
    engine.on_order_new(ts, pid, 20, true, 150'00, 1000, 0);
    engine.on_order_new(ts + 10'000'000ULL, pid, 21, false, 150'00, 1000, 0);

    auto alert = engine.evaluate_surveillance(pid, ts + 15'000'000ULL);
    assert(alert.is_alert_triggered);
    assert(alert.infraction_type == MarInfractionType::WashTrading);
    assert(alert.confidence_score >= 90);
}

void test_quote_stuffing() {
    EsmaMarSurveillanceEngine engine;
    uint64_t pid = 4004;
    uint64_t base_ts = 4'000'000'000ULL;

    // Flood 25 orders/cancels in a 2ms window
    for (uint64_t i = 1; i <= 25; ++i) {
        uint64_t t = base_ts + i * 50'000ULL; // each 50us
        engine.on_order_new(t, pid, 100 + i, true, 50'00 + i, 10, 0);
    }

    auto alert = engine.evaluate_surveillance(pid, base_ts + 2'000'000ULL);
    assert(alert.is_alert_triggered);
    assert(alert.infraction_type == MarInfractionType::QuoteStuffing);
    assert(alert.burst_message_rate_hz > 4000.0);
}

void test_marking_the_close() {
    EsmaMarSurveillanceEngine engine;
    uint64_t pid = 5005;
    uint64_t ts = 5'000'000'000ULL;

    // In market close window, 6 aggressive orders executed heavily
    for (uint64_t i = 1; i <= 6; ++i) {
        engine.on_order_new(ts + i * 1'000'000ULL, pid, 500 + i, true, 200'00 + i * 10, 100, 0);
        if (i <= 4) {
            engine.on_order_fill(ts + i * 1'000'000ULL + 500'000, pid, 500 + i, true, 200'00 + i * 10, 100);
        }
    }

    auto alert = engine.evaluate_surveillance(pid, ts + 10'000'000ULL, true /* market close window */);
    assert(alert.is_alert_triggered);
    assert(alert.infraction_type == MarInfractionType::MarkingTheClose);
}

int main() {
    test_normal_order_flow();
    test_layering_and_spoofing();
    test_wash_trading();
    test_quote_stuffing();
    test_marking_the_close();
    std::cout << "ESMA MAR Multi-Pattern Surveillance Engine tests passed.\n";
    return 0;
}
