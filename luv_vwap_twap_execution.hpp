#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

enum class ExecutionAlgoType : uint8_t {
    VWAP = 0,
    TWAP = 1
};

struct ExecutionSlicePlan {
    size_t slice_index{0};
    uint64_t slice_target_shares{0};
    uint64_t cumulative_target_shares{0};
    double target_percentage{0.0};
};

struct ExecutionPerformanceReport {
    uint64_t total_filled_shares{0};
    double executed_vwap{0.0};
    double market_vwap{0.0};
    double slippage_bps{0.0};          // (Exec VWAP - Market VWAP) / Market VWAP in bps
    double implementation_shortfall_usd{0.0};
    double participation_rate_pct{0.0};
    bool within_participation_limit{true}; // e.g. < 15% ADV participation
};

struct VWAPTWAPConfig {
    size_t num_intervals{10};          // 10 intraday buckets
    double max_participation_limit_pct{15.0}; // 15% ADV limit
    double arrival_benchmark_price{100.0};
};

class VWAPTWAPExecutionScheduler {
public:
    static constexpr size_t MAX_INTERVALS = 64;

    explicit VWAPTWAPExecutionScheduler(const VWAPTWAPConfig& cfg = VWAPTWAPConfig{}) noexcept
        : config_(cfg) {
        reset();
    }

    void reset() noexcept {
        total_executed_shares_ = 0;
        total_executed_notional_ = 0.0;
        market_volume_sum_ = 0;
        market_notional_sum_ = 0.0;
    }

    // Set custom historical volume curve for VWAP (must sum to > 0)
    void set_volume_profile(const double* weights, size_t count) noexcept {
        size_t n = std::min(count, MAX_INTERVALS);
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i) {
            volume_profile_[i] = weights[i];
            sum += weights[i];
        }
        if (sum > 0.0) {
            for (size_t i = 0; i < n; ++i) {
                volume_profile_[i] /= sum;
            }
        }
        profile_count_ = n;
    }

    // Generate schedule of slices for parent order
    std::array<ExecutionSlicePlan, MAX_INTERVALS> generate_schedule(
        ExecutionAlgoType algo,
        uint64_t parent_order_shares) const noexcept {

        std::array<ExecutionSlicePlan, MAX_INTERVALS> schedule{};
        size_t n = (profile_count_ > 0 && algo == ExecutionAlgoType::VWAP) ? profile_count_ : config_.num_intervals;
        n = std::min(n, MAX_INTERVALS);
        if (n == 0) return schedule;

        uint64_t cum_shares = 0;
        double cum_pct = 0.0;

        for (size_t i = 0; i < n; ++i) {
            double weight = (algo == ExecutionAlgoType::VWAP && profile_count_ > 0) 
                ? volume_profile_[i] : (1.0 / static_cast<double>(n));

            uint64_t slice_shares = 0;
            if (i == n - 1) {
                // Terminal slice absorbs rounding difference
                slice_shares = (parent_order_shares > cum_shares) ? (parent_order_shares - cum_shares) : 0;
            } else {
                slice_shares = static_cast<uint64_t>(std::round(static_cast<double>(parent_order_shares) * weight));
            }

            cum_shares += slice_shares;
            cum_pct += weight * 100.0;

            schedule[i].slice_index = i;
            schedule[i].slice_target_shares = slice_shares;
            schedule[i].cumulative_target_shares = cum_shares;
            schedule[i].target_percentage = cum_pct;
        }

        return schedule;
    }

    void record_fill(uint64_t fill_shares, double fill_price, uint64_t market_interval_vol, double market_interval_vwap) noexcept {
        total_executed_shares_ += fill_shares;
        total_executed_notional_ += static_cast<double>(fill_shares) * fill_price;

        market_volume_sum_ += market_interval_vol;
        market_notional_sum_ += static_cast<double>(market_interval_vol) * market_interval_vwap;
    }

    ExecutionPerformanceReport evaluate_performance() const noexcept {
        ExecutionPerformanceReport rep{};
        rep.total_filled_shares = total_executed_shares_;

        if (total_executed_shares_ > 0) {
            rep.executed_vwap = total_executed_notional_ / static_cast<double>(total_executed_shares_);
        }

        if (market_volume_sum_ > 0) {
            rep.market_vwap = market_notional_sum_ / static_cast<double>(market_volume_sum_);
            rep.participation_rate_pct = (static_cast<double>(total_executed_shares_) / static_cast<double>(market_volume_sum_)) * 100.0;
        }

        if (rep.market_vwap > 0.0 && rep.executed_vwap > 0.0) {
            rep.slippage_bps = ((rep.executed_vwap - rep.market_vwap) / rep.market_vwap) * 10'000.0;
        }

        // Implementation Shortfall: Executed Notional - (Filled Shares * Arrival Price)
        if (config_.arrival_benchmark_price > 0.0) {
            rep.implementation_shortfall_usd = total_executed_notional_ - 
                (static_cast<double>(total_executed_shares_) * config_.arrival_benchmark_price);
        }

        rep.within_participation_limit = (rep.participation_rate_pct <= config_.max_participation_limit_pct);
        return rep;
    }

private:
    VWAPTWAPConfig config_{};
    std::array<double, MAX_INTERVALS> volume_profile_{};
    size_t profile_count_{0};

    uint64_t total_executed_shares_{0};
    double total_executed_notional_{0.0};
    uint64_t market_volume_sum_{0};
    double market_notional_sum_{0.0};
};

} // namespace luv
