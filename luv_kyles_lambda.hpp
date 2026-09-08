#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

struct TradeObservation {
    int64_t signed_volume{0}; // Positive = Buy, Negative = Sell
    int64_t price_change{0};  // Price change after trade (in scaled fixed-point ticks/cents)
};

class KylesLambdaEstimator {
public:
    static constexpr size_t kWindowSize = 64;

    KylesLambdaEstimator() noexcept : count_(0), head_(0), lambda_(0.0), permanent_impact_ratio_(0.0) {}

    // Record an executed trade with subsequent price displacement
    void add_observation(int64_t signed_vol, int64_t price_change) noexcept {
        buffer_[head_] = {signed_vol, price_change};
        head_ = (head_ + 1) % kWindowSize;
        if (count_ < kWindowSize) ++count_;

        if (count_ >= 5) {
            recalculate_lambda();
        }
    }

    double get_lambda() const noexcept { return lambda_; }
    double get_permanent_impact_ratio() const noexcept { return permanent_impact_ratio_; }

private:
    void recalculate_lambda() noexcept {
        double sum_q = 0.0, sum_dp = 0.0;
        for (size_t i = 0; i < count_; ++i) {
            sum_q += static_cast<double>(buffer_[i].signed_volume);
            sum_dp += static_cast<double>(buffer_[i].price_change);
        }

        double mean_q = sum_q / static_cast<double>(count_);
        double mean_dp = sum_dp / static_cast<double>(count_);

        double cov_q_dp = 0.0;
        double var_q = 0.0;
        double var_dp = 0.0;

        for (size_t i = 0; i < count_; ++i) {
            double dq = static_cast<double>(buffer_[i].signed_volume) - mean_q;
            double ddp = static_cast<double>(buffer_[i].price_change) - mean_dp;
            cov_q_dp += dq * ddp;
            var_q += dq * dq;
            var_dp += ddp * ddp;
        }

        if (var_q > 1e-8) {
            lambda_ = cov_q_dp / var_q; // Kyle's Lambda (price change per share)
        } else {
            lambda_ = 0.0;
        }

        // Hasbrouck R^2 permanent impact ratio = (Cov)^2 / (Var_q * Var_dp)
        if (var_q > 1e-8 && var_dp > 1e-8) {
            permanent_impact_ratio_ = (cov_q_dp * cov_q_dp) / (var_q * var_dp);
            permanent_impact_ratio_ = std::clamp(permanent_impact_ratio_, 0.0, 1.0);
        } else {
            permanent_impact_ratio_ = 0.0;
        }
    }

    std::array<TradeObservation, kWindowSize> buffer_{};
    size_t count_{0};
    size_t head_{0};
    double lambda_{0.0};
    double permanent_impact_ratio_{0.0};
};

} // namespace luv
