#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace luv {

enum class TradingActivityLevel : uint8_t {
    HighFrequencyTrading = 0,    // HFT: Max divergence 100 us, timestamp granularity 1 us
    AlgorithmicTradingNonHFT = 1, // Non-HFT Algo: Max divergence 1 ms, granularity 1 ms
    VoiceManualTrading = 2       // Manual/Voice: Max divergence 1 s, granularity 1 s
};

enum class ClockSyncStatus : uint8_t {
    Synchronized = 0,            // Within regulatory tolerance
    WarningApproachingLimit = 1, // Divergence > 70% of regulatory tolerance
    BreachedNonCompliant = 2     // Divergence > tolerance (Trading MUST be halted)
};

struct Rts25ClockSample {
    uint64_t sample_timestamp_ns{0};
    int64_t ptp_offset_ns{0};          // Offset from UTC reference
    uint64_t round_trip_delay_ns{0};   // PTP network path delay
    uint32_t grandmaster_stratum{1};   // Stratum 1 = Atomic / GNSS locked
    bool grandmaster_locked{true};
};

struct Rts25ComplianceResult {
    ClockSyncStatus sync_status{ClockSyncStatus::Synchronized};
    int64_t current_divergence_ns{0};
    uint64_t max_allowable_divergence_ns{100'000ULL}; // 100 microseconds for HFT
    uint64_t required_timestamp_granularity_ns{1'000ULL}; // 1 microsecond
    bool can_route_orders{true};
    bool requires_kill_switch_halt{false};
    uint32_t violation_count{0};
};

class MifidRts25ClockSyncEngine {
public:
    static constexpr uint64_t kHftMaxDivergenceNs = 100'000ULL;        // 100 us
    static constexpr uint64_t kNonHftMaxDivergenceNs = 1'000'000ULL;   // 1 ms
    static constexpr uint64_t kVoiceMaxDivergenceNs = 1'000'000'000ULL; // 1 s

    static constexpr uint64_t kHftGranularityNs = 1'000ULL;            // 1 us
    static constexpr uint64_t kNonHftGranularityNs = 1'000'000ULL;     // 1 ms
    static constexpr uint64_t kVoiceGranularityNs = 1'000'000'000ULL;   // 1 s

    MifidRts25ClockSyncEngine(TradingActivityLevel activity_level = TradingActivityLevel::HighFrequencyTrading) noexcept
        : activity_level_(activity_level), violation_count_(0), last_sample_{} {}

    void on_ptp_clock_update(const Rts25ClockSample& sample) noexcept {
        last_sample_ = sample;
        int64_t abs_offset = std::abs(sample.ptp_offset_ns);
        uint64_t max_tol = get_max_tolerance_ns();

        if (abs_offset > static_cast<int64_t>(max_tol) || !sample.grandmaster_locked) {
            ++violation_count_;
        }
    }

    Rts25ComplianceResult evaluate_compliance() const noexcept {
        Rts25ComplianceResult res{};
        res.max_allowable_divergence_ns = get_max_tolerance_ns();
        res.required_timestamp_granularity_ns = get_granularity_ns();
        res.current_divergence_ns = last_sample_.ptp_offset_ns;
        res.violation_count = violation_count_;

        int64_t abs_divergence = std::abs(last_sample_.ptp_offset_ns);
        uint64_t warning_limit = static_cast<uint64_t>(res.max_allowable_divergence_ns * 0.70);

        if (!last_sample_.grandmaster_locked || abs_divergence > static_cast<int64_t>(res.max_allowable_divergence_ns)) {
            res.sync_status = ClockSyncStatus::BreachedNonCompliant;
            res.can_route_orders = false;
            res.requires_kill_switch_halt = true;
        } else if (abs_divergence >= static_cast<int64_t>(warning_limit)) {
            res.sync_status = ClockSyncStatus::WarningApproachingLimit;
            res.can_route_orders = true;
            res.requires_kill_switch_halt = false;
        } else {
            res.sync_status = ClockSyncStatus::Synchronized;
            res.can_route_orders = true;
            res.requires_kill_switch_halt = false;
        }

        return res;
    }

    void reset_violations() noexcept {
        violation_count_ = 0;
    }

private:
    TradingActivityLevel activity_level_{TradingActivityLevel::HighFrequencyTrading};
    uint32_t violation_count_{0};
    Rts25ClockSample last_sample_{};

    uint64_t get_max_tolerance_ns() const noexcept {
        switch (activity_level_) {
            case TradingActivityLevel::HighFrequencyTrading: return kHftMaxDivergenceNs;
            case TradingActivityLevel::AlgorithmicTradingNonHFT: return kNonHftMaxDivergenceNs;
            case TradingActivityLevel::VoiceManualTrading: return kVoiceMaxDivergenceNs;
        }
        return kHftMaxDivergenceNs;
    }

    uint64_t get_granularity_ns() const noexcept {
        switch (activity_level_) {
            case TradingActivityLevel::HighFrequencyTrading: return kHftGranularityNs;
            case TradingActivityLevel::AlgorithmicTradingNonHFT: return kNonHftGranularityNs;
            case TradingActivityLevel::VoiceManualTrading: return kVoiceGranularityNs;
        }
        return kHftGranularityNs;
    }
};

} // namespace luv
