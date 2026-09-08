#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

enum class StatArbSignal : int8_t {
    ShortSpread = -1, // Spread overextended high -> sell A, buy B
    Neutral = 0,      // Inside band / exit position
    LongSpread = 1    // Spread overextended low -> buy A, sell B
};

struct OuParameters {
    double theta{0.0};       // Speed of mean reversion
    double mu{0.0};          // Long-term equilibrium mean
    double sigma_ou{0.0};    // Stationary standard deviation
    double current_zscore{0.0};
    StatArbSignal signal{StatArbSignal::Neutral};
};

class StatArbOuEngine {
public:
    static constexpr size_t kWindowSize = 64;

    explicit StatArbOuEngine(double entry_z = 2.0, double exit_z = 0.25) noexcept
        : entry_z_threshold_(entry_z), exit_z_threshold_(exit_z), count_(0), head_(0) {}

    // Add a new spread observation X_t = Price(A) - hedge_ratio * Price(B)
    void update_spread(double spread) noexcept {
        buffer_[head_] = spread;
        head_ = (head_ + 1) % kWindowSize;
        if (count_ < kWindowSize) ++count_;

        if (count_ >= 10) {
            fit_ou_process();
        }
    }

    const OuParameters& get_params() const noexcept { return params_; }

private:
    void fit_ou_process() noexcept {
        // AR(1) linear regression on consecutive pairs: X_{t+1} = a * X_t + b + eps
        size_t n = count_ - 1;
        double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;

        for (size_t i = 0; i < n; ++i) {
            size_t idx_x = (head_ + kWindowSize - count_ + i) % kWindowSize;
            size_t idx_y = (idx_x + 1) % kWindowSize;

            double x = buffer_[idx_x];
            double y = buffer_[idx_y];

            sum_x += x;
            sum_y += y;
            sum_xx += x * x;
            sum_xy += x * y;
        }

        double denom = (n * sum_xx - sum_x * sum_x);
        if (std::abs(denom) < 1e-12) return;

        double a = (n * sum_xy - sum_x * sum_y) / denom;
        double b = (sum_y - a * sum_x) / n;

        // Ensure mean-reverting (|a| < 1.0)
        a = std::clamp(a, 0.0001, 0.9999);
        double dt = 1.0; // Normalized 1 step

        params_.theta = -std::log(a) / dt;
        params_.mu = b / (1.0 - a);

        // Residual sum of squares
        double sse = 0.0;
        for (size_t i = 0; i < n; ++i) {
            size_t idx_x = (head_ + kWindowSize - count_ + i) % kWindowSize;
            size_t idx_y = (idx_x + 1) % kWindowSize;
            double pred = a * buffer_[idx_x] + b;
            double err = buffer_[idx_y] - pred;
            sse += err * err;
        }
        double var_eps = sse / std::max(1.0, static_cast<double>(n - 2));
        double sigma_sq = var_eps * (2.0 * params_.theta) / (1.0 - a * a);
        params_.sigma_ou = (sigma_sq > 0.0 && params_.theta > 0.0) ? std::sqrt(sigma_sq / (2.0 * params_.theta)) : 1.0;

        if (params_.sigma_ou < 1e-6) params_.sigma_ou = 1e-6;

        // Current latest spread
        size_t latest_idx = (head_ + kWindowSize - 1) % kWindowSize;
        double latest_val = buffer_[latest_idx];

        params_.current_zscore = (latest_val - params_.mu) / params_.sigma_ou;

        // Signal generation
        if (params_.current_zscore >= entry_z_threshold_) {
            params_.signal = StatArbSignal::ShortSpread;
        } else if (params_.current_zscore <= -entry_z_threshold_) {
            params_.signal = StatArbSignal::LongSpread;
        } else if (std::abs(params_.current_zscore) <= exit_z_threshold_) {
            params_.signal = StatArbSignal::Neutral;
        }
    }

    double entry_z_threshold_{2.0};
    double exit_z_threshold_{0.25};

    std::array<double, kWindowSize> buffer_{};
    size_t count_{0};
    size_t head_{0};

    OuParameters params_{};
};

} // namespace luv
