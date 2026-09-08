#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct LeadLagResult {
    int32_t optimal_lag_steps{0}; // > 0 means Series X leads Series Y; < 0 means Series Y leads Series X
    double max_correlation{0.0};
    double synchronous_correlation{0.0};
    bool significant_lead_lag{false};
};

struct LeadLagConfig {
    static constexpr int32_t MAX_LAG = 10;
    double significance_threshold{0.25}; // Min correlation difference (|rho(tau*) - rho(0)| > threshold)
};

class LeadLagCrossCorrelationEngine {
public:
    static constexpr size_t MAX_POINTS = 512;
    static constexpr int32_t MAX_LAG = 10;

    explicit LeadLagCrossCorrelationEngine(const LeadLagConfig& cfg = LeadLagConfig{}) noexcept
        : config_(cfg) {
        reset();
    }

    void reset() noexcept {
        count_ = 0;
    }

    bool add_synchronized_point(double return_x, double return_y) noexcept {
        if (count_ >= MAX_POINTS) {
            for (size_t i = 1; i < MAX_POINTS; ++i) {
                x_[i - 1] = x_[i];
                y_[i - 1] = y_[i];
            }
            x_[MAX_POINTS - 1] = return_x;
            y_[MAX_POINTS - 1] = return_y;
            return true;
        }

        x_[count_] = return_x;
        y_[count_] = return_y;
        ++count_;
        return true;
    }

    LeadLagResult compute_lead_lag() const noexcept {
        LeadLagResult result{};
        if (count_ < static_cast<size_t>(2 * MAX_LAG + 5)) {
            return result;
        }

        // Calculate means and variances for X and Y
        double sum_x = 0.0, sum_y = 0.0;
        double sum_sq_x = 0.0, sum_sq_y = 0.0;
        for (size_t i = 0; i < count_; ++i) {
            sum_x += x_[i];
            sum_y += y_[i];
            sum_sq_x += x_[i] * x_[i];
            sum_sq_y += y_[i] * y_[i];
        }

        double n = static_cast<double>(count_);
        double mean_x = sum_x / n;
        double mean_y = sum_y / n;
        double var_x = (sum_sq_x / n) - (mean_x * mean_x);
        double var_y = (sum_sq_y / n) - (mean_y * mean_y);

        if (var_x <= 1e-12 || var_y <= 1e-12) {
            return result;
        }

        double std_xy = std::sqrt(var_x * var_y);

        int32_t best_lag = 0;
        double max_corr = -1.0;
        double synch_corr = 0.0;

        for (int32_t lag = -MAX_LAG; lag <= MAX_LAG; ++lag) {
            double cross_sum = 0.0;
            size_t valid_pairs = 0;

            for (size_t i = 0; i < count_; ++i) {
                int64_t j = static_cast<int64_t>(i) + lag;
                if (j >= 0 && j < static_cast<int64_t>(count_)) {
                    cross_sum += (x_[i] - mean_x) * (y_[j] - mean_y);
                    ++valid_pairs;
                }
            }

            if (valid_pairs > 0) {
                double cov = cross_sum / static_cast<double>(valid_pairs);
                double corr = cov / std_xy;

                if (lag == 0) {
                    synch_corr = corr;
                }

                if (std::abs(corr) > max_corr) {
                    max_corr = std::abs(corr);
                    best_lag = lag;
                    result.max_correlation = corr;
                }
            }
        }

        result.optimal_lag_steps = best_lag;
        result.synchronous_correlation = synch_corr;
        result.significant_lead_lag = (best_lag != 0) && (max_corr - std::abs(synch_corr) >= config_.significance_threshold);

        return result;
    }

    size_t count() const noexcept { return count_; }

private:
    LeadLagConfig config_{};
    std::array<double, MAX_POINTS> x_{};
    std::array<double, MAX_POINTS> y_{};
    size_t count_{0};
};

} // namespace luv
