#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct CMOResult {
    double cmo_value{0.0};              // Range [-100.0, +100.0]
    double sum_up{0.0};
    double sum_down{0.0};
    double dynamic_spread_multiplier{1.0};
    bool overbought{false};             // CMO >= +50.0
    bool oversold{false};               // CMO <= -50.0
    bool is_valid{false};
};

class ChandeMomentumOscillator {
public:
    static constexpr size_t MAX_PERIOD = 32;

    explicit ChandeMomentumOscillator(size_t period = 14, double spread_scale_beta = 0.5) noexcept
        : period_(std::min(period, MAX_PERIOD)), spread_scale_beta_(spread_scale_beta) {
        reset();
    }

    void reset() noexcept {
        price_count_ = 0;
    }

    CMOResult update(double price) noexcept {
        CMOResult res{};
        if (price <= 0.0) return res;

        if (price_count_ >= MAX_PERIOD + 1) {
            for (size_t i = 1; i < MAX_PERIOD + 1; ++i) {
                prices_[i - 1] = prices_[i];
            }
            prices_[MAX_PERIOD] = price;
        } else {
            prices_[price_count_++] = price;
        }

        if (price_count_ < period_ + 1) {
            return res;
        }

        double sum_u = 0.0;
        double sum_d = 0.0;

        size_t end_idx = price_count_ - 1;
        size_t start_idx = end_idx - period_;

        for (size_t i = start_idx; i < end_idx; ++i) {
            double diff = prices_[i + 1] - prices_[i];
            if (diff > 0.0) {
                sum_u += diff;
            } else if (diff < 0.0) {
                sum_d += -diff;
            }
        }

        res.sum_up = sum_u;
        res.sum_down = sum_d;

        double total_movement = sum_u + sum_d;
        if (total_movement > 1e-9) {
            res.cmo_value = 100.0 * (sum_u - sum_d) / total_movement;
        } else {
            res.cmo_value = 0.0;
        }

        res.overbought = (res.cmo_value >= 50.0);
        res.oversold = (res.cmo_value <= -50.0);
        res.dynamic_spread_multiplier = 1.0 + (std::abs(res.cmo_value) / 100.0) * spread_scale_beta_;
        res.is_valid = true;

        return res;
    }

private:
    size_t period_{14};
    double spread_scale_beta_{0.5};
    std::array<double, MAX_PERIOD + 1> prices_{};
    size_t price_count_{0};
};

} // namespace luv
