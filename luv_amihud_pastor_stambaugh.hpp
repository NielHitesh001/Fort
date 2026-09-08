#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct DailyMarketObservation {
    double daily_return{0.0};      // r_t (e.g. +0.015 for +1.5%)
    double dollar_volume{0.0};     // V_t in USD
};

struct AmihudPSResult {
    double amihud_illiq_ratio{0.0};  // Amihud ILLIQ * 10^6
    double pastor_stambaugh_gamma{0.0}; // Gamma coefficient (adverse price concession reversal)
    double mean_daily_volume{0.0};
    size_t observation_count{0};
    bool illiquid_anomaly_detected{false};
};

class AmihudPastorStambaughEngine {
public:
    static constexpr size_t MAX_OBSERVATIONS = 256;

    AmihudPastorStambaughEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        count_ = 0;
    }

    bool add_observation(double daily_return, double dollar_volume) noexcept {
        if (dollar_volume <= 0.0) return false;

        if (count_ >= MAX_OBSERVATIONS) {
            for (size_t i = 1; i < MAX_OBSERVATIONS; ++i) {
                obs_[i - 1] = obs_[i];
            }
            obs_[MAX_OBSERVATIONS - 1] = {daily_return, dollar_volume};
            return true;
        }

        obs_[count_++] = {daily_return, dollar_volume};
        return true;
    }

    AmihudPSResult compute_metrics() const noexcept {
        AmihudPSResult res{};
        res.observation_count = count_;
        if (count_ < 5) return res;

        // 1. Amihud (2002) ILLIQ
        double sum_illiq = 0.0;
        double sum_vol = 0.0;
        for (size_t i = 0; i < count_; ++i) {
            sum_illiq += std::abs(obs_[i].daily_return) / obs_[i].dollar_volume;
            sum_vol += obs_[i].dollar_volume;
        }
        res.amihud_illiq_ratio = (sum_illiq / static_cast<double>(count_)) * 1'000'000.0; // Scaled by 1M
        res.mean_daily_volume = sum_vol / static_cast<double>(count_);

        // 2. Pastor-Stambaugh (2003) Gamma: Regression of r_{t+1} on r_t and sign(r_t)*Volume_M
        size_t n = count_ - 1; // Number of lagged pairs
        std::array<double, MAX_OBSERVATIONS> y{};  // r_{t+1}
        std::array<double, MAX_OBSERVATIONS> x1{}; // r_t
        std::array<double, MAX_OBSERVATIONS> x2{}; // sign(r_t) * (V_t / 1M)

        double sum_y = 0.0, sum_x1 = 0.0, sum_x2 = 0.0;
        for (size_t t = 0; t < n; ++t) {
            y[t] = obs_[t + 1].daily_return;
            x1[t] = obs_[t].daily_return;
            double sgn = (obs_[t].daily_return > 0.0) ? 1.0 : ((obs_[t].daily_return < 0.0) ? -1.0 : 0.0);
            x2[t] = sgn * (obs_[t].dollar_volume / 1'000'000.0); // In Millions

            sum_y += y[t];
            sum_x1 += x1[t];
            sum_x2 += x2[t];
        }

        double n_d = static_cast<double>(n);
        double my = sum_y / n_d;
        double mx1 = sum_x1 / n_d;
        double mx2 = sum_x2 / n_d;

        // Compute Covariances and Variances
        double var_x1 = 0.0, var_x2 = 0.0;
        double cov_x1_x2 = 0.0, cov_y_x1 = 0.0, cov_y_x2 = 0.0;

        for (size_t t = 0; t < n; ++t) {
            double dy = y[t] - my;
            double dx1 = x1[t] - mx1;
            double dx2 = x2[t] - mx2;

            var_x1 += dx1 * dx1;
            var_x2 += dx2 * dx2;
            cov_x1_x2 += dx1 * dx2;
            cov_y_x1 += dy * dx1;
            cov_y_x2 += dy * dx2;
        }

        // Multiple regression coefficient for x2 (gamma):
        // gamma = (cov(y, x2)*var(x1) - cov(y, x1)*cov(x1, x2)) / (var(x1)*var(x2) - cov(x1, x2)^2)
        double denom = (var_x1 * var_x2) - (cov_x1_x2 * cov_x1_x2);
        if (std::abs(denom) > 1e-12) {
            res.pastor_stambaugh_gamma = ((cov_y_x2 * var_x1) - (cov_y_x1 * cov_x1_x2)) / denom;
        } else {
            res.pastor_stambaugh_gamma = 0.0;
        }

        // Anomaly: extreme illiquidity if Amihud ILLIQ > 1.0 (for large cap) or severe negative gamma
        res.illiquid_anomaly_detected = (res.amihud_illiq_ratio > 2.0) || (res.pastor_stambaugh_gamma < -0.05);

        return res;
    }

    size_t count() const noexcept { return count_; }

private:
    std::array<DailyMarketObservation, MAX_OBSERVATIONS> obs_{};
    size_t count_{0};
};

} // namespace luv
