#include "luv_mifid_rts25_clock_sync.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_hft_clock_sync_compliant() {
    MifidRts25ClockSyncEngine engine(TradingActivityLevel::HighFrequencyTrading);

    Rts25ClockSample sample{};
    sample.sample_timestamp_ns = 1'000'000'000ULL;
    sample.ptp_offset_ns = 25'000; // 25 us offset (well below 100 us SLA)
    sample.round_trip_delay_ns = 10'000;
    sample.grandmaster_stratum = 1;
    sample.grandmaster_locked = true;

    engine.on_ptp_clock_update(sample);
    auto res = engine.evaluate_compliance();

    assert(res.sync_status == ClockSyncStatus::Synchronized);
    assert(res.can_route_orders == true);
    assert(res.requires_kill_switch_halt == false);
    assert(res.max_allowable_divergence_ns == 100'000ULL);
    assert(res.required_timestamp_granularity_ns == 1'000ULL);
    assert(res.violation_count == 0);
}

void test_hft_clock_sync_breach_and_kill_switch() {
    MifidRts25ClockSyncEngine engine(TradingActivityLevel::HighFrequencyTrading);

    Rts25ClockSample sample{};
    sample.sample_timestamp_ns = 2'000'000'000ULL;
    sample.ptp_offset_ns = 125'000; // 125 us offset (> 100 us maximum tolerance)
    sample.grandmaster_locked = true;

    engine.on_ptp_clock_update(sample);
    auto res = engine.evaluate_compliance();

    assert(res.sync_status == ClockSyncStatus::BreachedNonCompliant);
    assert(res.can_route_orders == false);
    assert(res.requires_kill_switch_halt == true);
    assert(res.violation_count == 1);
}

void test_grandmaster_unlock_triggers_halt() {
    MifidRts25ClockSyncEngine engine(TradingActivityLevel::HighFrequencyTrading);

    Rts25ClockSample sample{};
    sample.sample_timestamp_ns = 3'000'000'000ULL;
    sample.ptp_offset_ns = 5'000;
    sample.grandmaster_locked = false; // GNSS loss of lock

    engine.on_ptp_clock_update(sample);
    auto res = engine.evaluate_compliance();

    assert(res.sync_status == ClockSyncStatus::BreachedNonCompliant);
    assert(res.can_route_orders == false);
    assert(res.requires_kill_switch_halt == true);
}

int main() {
    test_hft_clock_sync_compliant();
    test_hft_clock_sync_breach_and_kill_switch();
    test_grandmaster_unlock_triggers_halt();
    std::cout << "MiFID II RTS 25 Clock Synchronization & Traceability Engine tests passed.\n";
    return 0;
}
