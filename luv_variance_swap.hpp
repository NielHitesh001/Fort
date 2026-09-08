#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

struct VarianceSwapTerms {
    double strike_variance{0.04};      // Strike variance K_var = sigma_K^2 (e.g. 0.04 = 20% vol)
    double notional_vega{10000.0};     // Vega notional in USD per 1 vol point ($)
    double time_to_maturity_years{1.0};// Maturity T
    uint32_t total_expected_observations{252}; // E.g. 252 daily business days
};

class VarianceSwapEngine {
public:
    static constexpr size_t kMaxObservations = 256;

    explicit VarianceSwapEngine(const VarianceSwapTerms& terms = {}) noexcept
        : terms_(terms), obs_count_(0) {}

    void add_price_observation(double spot_price) noexcept {
        if (spot_price <= 0.0 || obs_count_ >= kMaxObservations) return;
        prices_[obs_count_++] = spot_price;
    }

    // Calculates realized annual variance: sigma_R^2 = (252 / N) * sum(ln(S_i / S_{i-1})^2)
    double compute_realized_variance() const noexcept {
        if (obs_count_ < 2) return 0.0;

        double sum_log_ret_sq = 0.0;
        for (size_t i = 1; i < obs_count_; ++i) {
            double log_ret = std::log(prices_[i] / prices_[i - 1]);
            sum_log_ret_sq += log_ret * log_ret;
        }

        size_t num_returns = obs_count_ - 1;
        double annual_factor = static_cast<double>(terms_.total_expected_observations) / static_cast<double>(num_returns);
        return annual_factor * sum_log_ret_sq;
    }

    // Realized volatility sigma_R = sqrt(sigma_R^2)
    double compute_realized_volatility() const noexcept {
        double var = compute_realized_variance();
        return (var > 0.0) ? std::sqrt(var) : 0.0;
    }

    // Variance swap settlement payoff = Variance Notional * (sigma_R^2 - sigma_K^2)
    // Variance Notional N_var = Vega Notional / (2 * sqrt(sigma_K^2))
    double compute_settlement_pnl() const noexcept {
        double strike_vol = std::sqrt(std::max(1e-6, terms_.strike_variance));
        double variance_notional = terms_.notional_vega / (2.0 * strike_vol * 100.0); // Scaled in percentage points

        double real_var = compute_realized_variance();
        double var_diff_pts = (real_var - terms_.strike_variance) * 10000.0; // In vol points squared

        return variance_notional * var_diff_pts;
    }

    const VarianceSwapTerms& get_terms() const noexcept { return terms_; }

private:
    VarianceSwapTerms terms_;
    std::array<double, kMaxObservations> prices_{};
    size_t obs_count_{0};
};

} // namespace luv
