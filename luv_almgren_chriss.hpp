#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

struct AlmgrenChrissParams {
    double total_shares{10000.0};    // Initial inventory X_0 to liquidate
    double time_horizon_sec{300.0};  // Total execution window T (e.g. 5 minutes)
    double volatility_sigma{0.02};   // Volatility
    double risk_aversion_lambda{1e-4};// Risk aversion parameter
    double temp_impact_eta{2.5e-4};  // Temporary market impact coefficient
    double perm_impact_gamma{1e-5};  // Permanent market impact coefficient
    size_t num_intervals{10};        // Discretization intervals N
};

struct ExecutionStep {
    double time_sec{0.0};
    double target_holdings{0.0};
    double trade_shares{0.0};
    double trading_rate{0.0}; // Shares per second
};

class AlmgrenChrissModel {
public:
    static constexpr size_t kMaxSteps = 32;

    static size_t compute_optimal_trajectory(
        const AlmgrenChrissParams& p,
        ExecutionStep* out_steps,
        size_t max_out) noexcept
    {
        if (!out_steps || max_out == 0 || p.num_intervals == 0) return 0;

        size_t n_steps = std::min(p.num_intervals, std::min(max_out, kMaxSteps));
        double T = p.time_horizon_sec;
        double tau = T / static_cast<double>(n_steps);

        // Compute urgency parameter kappa: kappa^2 ~ (lambda * sigma^2) / eta
        double kappa_sq = (p.risk_aversion_lambda * p.volatility_sigma * p.volatility_sigma) / p.temp_impact_eta;
        double kappa = std::sqrt(std::max(1e-8, kappa_sq));

        double sinh_kappa_T = std::sinh(kappa * T);
        if (std::abs(sinh_kappa_T) < 1e-10) sinh_kappa_T = 1e-10;

        double prev_holdings = p.total_shares;

        for (size_t j = 0; j <= n_steps; ++j) {
            double t_j = static_cast<double>(j) * tau;
            double holdings = 0.0;

            if (j == n_steps) {
                holdings = 0.0;
            } else {
                holdings = p.total_shares * (std::sinh(kappa * (T - t_j)) / sinh_kappa_T);
            }

            out_steps[j].time_sec = t_j;
            out_steps[j].target_holdings = holdings;
            
            if (j > 0) {
                out_steps[j].trade_shares = prev_holdings - holdings;
                out_steps[j].trading_rate = out_steps[j].trade_shares / tau;
            } else {
                out_steps[j].trade_shares = 0.0;
                out_steps[j].trading_rate = 0.0;
            }

            prev_holdings = holdings;
        }

        return n_steps + 1;
    }
};

} // namespace luv
