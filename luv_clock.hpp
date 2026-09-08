#pragma once

#include <cstdint>
#include <chrono>
#include <atomic>
#include <ctime>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#elif defined(__aarch64__) || defined(__arm64__)
#include <mach/mach_time.h>
#endif

namespace luv {

// MiFID II RTS 25 specifies maximum divergence limits from UTC:
// - High-frequency algorithmic trading: 100 microseconds (100,000 ns)
// - Non-HFT algorithmic trading: 1 millisecond (1,000,000 ns)
struct ClockCalibrationConfig {
    uint64_t max_allowed_drift_ns = 100'000; // 100 microseconds
    uint32_t calibration_samples = 1000;
};

class HardwareClock {
public:
    HardwareClock() noexcept {
        calibrate();
    }

    // Read hardware cycle counter
    static inline uint64_t rdtsc() noexcept {
#if defined(__APPLE__)
        return static_cast<uint64_t>(mach_absolute_time());
#elif defined(__x86_64__) || defined(_M_X64)
        return __rdtsc();
#elif defined(__aarch64__) || defined(__arm64__)
        uint64_t val;
        asm volatile("isb; mrs %0, cntvct_el0" : "=r"(val));
        return val;
#else
        return static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
#endif
    }

    // Calibrate TSC cycles against system realtime wall-clock
    void calibrate() noexcept {
        const auto t0_wall = std::chrono::system_clock::now();
        const uint64_t c0 = rdtsc();

        // Busy loop or short delay to calculate cycles per nanosecond
        const auto target = t0_wall + std::chrono::microseconds(1000); // 1ms sample
        while (std::chrono::system_clock::now() < target) {
            // spin
        }

        const auto t1_wall = std::chrono::system_clock::now();
        const uint64_t c1 = rdtsc();

        const uint64_t elapsed_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1_wall - t0_wall).count());
        const uint64_t cycles_delta = c1 - c0;

        if (elapsed_ns > 0 && cycles_delta > 0) {
            cycles_per_ns_scaled_ = (cycles_delta << 16) / elapsed_ns;
            ns_per_cycle_scaled_ = (elapsed_ns << 16) / cycles_delta;
        } else {
            cycles_per_ns_scaled_ = 1 << 16;
            ns_per_cycle_scaled_ = 1 << 16;
        }

        base_wall_ns_ = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                t0_wall.time_since_epoch()).count());
        base_cycle_ = c0;
        last_monotonic_ns_.store(base_wall_ns_, std::memory_order_relaxed);
    }

    // Returns cycle-derived UTC timestamp in nanoseconds since UNIX epoch
    // Guaranteed strictly monotonic
    inline uint64_t now_utc_ns() noexcept {
        const uint64_t current_cycle = rdtsc();
        const uint64_t delta_cycles = (current_cycle >= base_cycle_) ? (current_cycle - base_cycle_) : 0;
        const uint64_t elapsed_ns = (delta_cycles * ns_per_cycle_scaled_) >> 16;
        const uint64_t candidate_ns = base_wall_ns_ + elapsed_ns;

        // Ensure monotonicity
        uint64_t prev = last_monotonic_ns_.load(std::memory_order_relaxed);
        while (candidate_ns > prev) {
            if (last_monotonic_ns_.compare_exchange_weak(prev, candidate_ns, std::memory_order_relaxed)) {
                return candidate_ns;
            }
        }
        return prev + 1;
    }

    // Checks current clock drift against system realtime clock for MiFID II RTS 25 compliance
    bool verify_rts25_compliance(const ClockCalibrationConfig& config, int64_t* out_drift_ns = nullptr) noexcept {
        const auto sys_now = std::chrono::system_clock::now();
        const uint64_t sys_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(sys_now.time_since_epoch()).count());
        const uint64_t hw_ns = now_utc_ns();

        const int64_t drift = static_cast<int64_t>(hw_ns) - static_cast<int64_t>(sys_ns);
        const uint64_t abs_drift = (drift < 0) ? static_cast<uint64_t>(-drift) : static_cast<uint64_t>(drift);

        if (out_drift_ns) {
            *out_drift_ns = drift;
        }

        return abs_drift <= config.max_allowed_drift_ns;
    }

    // Direct conversion of cycle delta to nanoseconds
    inline uint64_t cycles_to_ns(uint64_t cycles) const noexcept {
        return (cycles * ns_per_cycle_scaled_) >> 16;
    }

private:
    uint64_t cycles_per_ns_scaled_{1 << 16};
    uint64_t ns_per_cycle_scaled_{1 << 16};
    uint64_t base_cycle_{0};
    uint64_t base_wall_ns_{0};
    std::atomic<uint64_t> last_monotonic_ns_{0};
};

} // namespace luv
