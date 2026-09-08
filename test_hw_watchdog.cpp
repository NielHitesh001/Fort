#include "luv_hw_watchdog.hpp"
#include <cassert>
#include <cstdio>

void test_hw_latency_and_jitter_watchdog() {
    luv::hw::HardwareLatencyWatchdog watchdog(2000); // 2000ns threshold

    // Normal fast samples (500ns - 900ns)
    for (uint64_t i = 1; i <= 95; ++i) {
        watchdog.record_sample(i * 1000, 500 + (i % 400), 2);
    }

    // Jitter anomaly spikes (>2000ns, e.g. OS interrupt / page fault / SMM tick)
    watchdog.record_sample(96'000, 2500, 2);
    watchdog.record_sample(97'000, 3100, 2);
    watchdog.record_sample(98'000, 5200, 2);
    watchdog.record_sample(99'000, 1800, 2);
    watchdog.record_sample(100'000, 800, 2);

    auto report = watchdog.generate_report();

    assert(report.total_samples == 100);
    assert(report.anomaly_count == 3);
    assert(report.core_isolation_violated == true);
    assert(report.max_latency_ns == 5200);
    assert(report.p50_latency_ns < 1000);

    std::printf("[PASS] test_hw_latency_and_jitter_watchdog (Samples: %llu, Anomalies: %llu, p50: %lluns, p99: %lluns, max: %lluns)\n",
        static_cast<unsigned long long>(report.total_samples),
        static_cast<unsigned long long>(report.anomaly_count),
        static_cast<unsigned long long>(report.p50_latency_ns),
        static_cast<unsigned long long>(report.p99_latency_ns),
        static_cast<unsigned long long>(report.max_latency_ns));
}

int main() {
    test_hw_latency_and_jitter_watchdog();
    std::printf("All hardware latency watchdog tests passed successfully.\n");
    return 0;
}
