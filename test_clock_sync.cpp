#include "luv_clock.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

void test_hardware_clock_monotonicity() {
    luv::HardwareClock clock;
    uint64_t prev = clock.now_utc_ns();
    assert(prev > 0);

    for (int i = 0; i < 10000; ++i) {
        uint64_t curr = clock.now_utc_ns();
        assert(curr >= prev);
        prev = curr;
    }
    std::printf("[PASS] test_hardware_clock_monotonicity\n");
}

void test_rts25_compliance() {
    luv::HardwareClock clock;
    luv::ClockCalibrationConfig config;
    config.max_allowed_drift_ns = 5'000'000; // 5ms tolerance for test runner

    int64_t drift = 0;
    bool compliant = clock.verify_rts25_compliance(config, &drift);
    assert(compliant);
    std::printf("[PASS] test_rts25_compliance (Drift: %lld ns)\n", static_cast<long long>(drift));
}

void test_cycles_to_ns() {
    luv::HardwareClock clock;
    uint64_t c0 = luv::HardwareClock::rdtsc();
    assert(c0 > 0);
    uint64_t ns = clock.cycles_to_ns(1000);
    assert(ns > 0);
    std::printf("[PASS] test_cycles_to_ns (1000 cycles = %llu ns)\n", static_cast<unsigned long long>(ns));
}

int main() {
    test_hardware_clock_monotonicity();
    test_rts25_compliance();
    test_cycles_to_ns();
    std::printf("All clock synchronization tests passed successfully.\n");
    return 0;
}
