#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {

struct AdaptiveCollarConfig {
    double min_collar_bps = 50.0;    // Minimum 50 bps collar (0.50%)
    double max_collar_bps = 500.0;   // Maximum 500 bps collar (5.00%)
    double volatility_multiplier = 3.0; // 3-sigma band
    double ema_alpha = 0.05;         // Weight for EMA price updates
};

class AdaptiveVolatilityCollar {
public:
    explicit AdaptiveVolatilityCollar(const AdaptiveCollarConfig& config = AdaptiveCollarConfig{}) noexcept
        : config_(config) {}

    void on_trade(int64_t trade_price) noexcept {
        const double p = static_cast<double>(trade_price);
        if (!initialized_) {
            ema_price_ = p;
            ema_variance_ = 0.0;
            initialized_ = true;
            return;
        }

        const double diff = p - ema_price_;
        ema_price_ += config_.ema_alpha * diff;
        // Exponential moving variance
        const double diff_sq = diff * diff;
        ema_variance_ += config_.ema_alpha * (diff_sq - ema_variance_);
    }

    // Calculates dynamic upper and lower price collar boundaries
    bool compute_collar_bounds(
        int64_t reference_price,
        int64_t& out_lower_bound,
        int64_t& out_upper_bound,
        double* out_collar_bps = nullptr) const noexcept
    {
        if (reference_price <= 0) return false;

        double std_dev = std::sqrt(std::max(0.0, ema_variance_));
        double ref = static_cast<double>(reference_price);

        // Volatility expressed in basis points: (std_dev / ref) * 10000
        double vol_bps = (ref > 0.0) ? (std_dev / ref) * 10000.0 : 0.0;
        double dynamic_bps = vol_bps * config_.volatility_multiplier;

        // Clamp collar within bounds
        double collar_bps = std::clamp(dynamic_bps, config_.min_collar_bps, config_.max_collar_bps);

        if (out_collar_bps) *out_collar_bps = collar_bps;

        const int64_t collar_offset = static_cast<int64_t>(std::round(ref * (collar_bps / 10000.0)));

        out_lower_bound = reference_price - collar_offset;
        out_upper_bound = reference_price + collar_offset;

        if (out_lower_bound < 1) out_lower_bound = 1;
        return true;
    }

    // Validate inbound order price against dynamic collar
    bool validate_price(int64_t order_price, int64_t reference_price, uint8_t side) const noexcept {
        int64_t lower = 0;
        int64_t upper = 0;
        if (!compute_collar_bounds(reference_price, lower, upper)) return false;

        if (side == exec::kBuy) {
            // Aggressive buy cannot exceed upper collar
            return order_price <= upper;
        } else {
            // Aggressive sell cannot fall below lower collar
            return order_price >= lower;
        }
    }

    double current_volatility_bps() const noexcept {
        if (!initialized_ || ema_price_ <= 0.0) return 0.0;
        return (std::sqrt(std::max(0.0, ema_variance_)) / ema_price_) * 10000.0;
    }

private:
    AdaptiveCollarConfig config_;
    bool initialized_{false};
    double ema_price_{0.0};
    double ema_variance_{0.0};
};

} // namespace luv
