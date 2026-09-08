#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {
namespace risk {

struct VarEstimate {
    double var_95_notional = 0.0; // 95% Confidence VaR
    double var_99_notional = 0.0; // 99% Confidence VaR
    double expected_shortfall = 0.0; // CVaR (Conditional VaR)
};

class PortfolioVarEngine {
public:
    // Parametric VaR assuming normal distribution of returns
    static VarEstimate compute_parametric_var(
        double portfolio_notional,
        double annualized_volatility,
        double holding_period_days = 1.0) noexcept
    {
        VarEstimate est;
        if (portfolio_notional <= 0.0 || annualized_volatility <= 0.0) return est;

        // Daily volatility = annualized_vol / sqrt(252)
        const double daily_vol = annualized_volatility * std::sqrt(holding_period_days / 252.0);

        // Z-scores: 95% = 1.644853, 99% = 2.326348
        est.var_95_notional = portfolio_notional * daily_vol * 1.644853;
        est.var_99_notional = portfolio_notional * daily_vol * 2.326348;
        // Expected Shortfall (CVaR) for 99% normal distribution = notional * daily_vol * (pdf(z) / (1 - alpha))
        est.expected_shortfall = portfolio_notional * daily_vol * (0.02665 / 0.01);

        return est;
    }

    // Historical VaR using sample return distribution
    template <size_t N>
    static VarEstimate compute_historical_var(
        double portfolio_notional,
        const std::array<double, N>& historical_daily_returns) noexcept
    {
        VarEstimate est;
        if (portfolio_notional <= 0.0 || N == 0) return est;

        std::array<double, N> sorted_returns = historical_daily_returns;
        std::sort(sorted_returns.begin(), sorted_returns.end());

        // 95% index = floor(0.05 * N)
        size_t idx_95 = static_cast<size_t>(0.05 * N);
        // 99% index = floor(0.01 * N)
        size_t idx_99 = static_cast<size_t>(0.01 * N);

        double loss_95 = (sorted_returns[idx_95] < 0.0) ? -sorted_returns[idx_95] : 0.0;
        double loss_99 = (sorted_returns[idx_99] < 0.0) ? -sorted_returns[idx_99] : 0.0;

        est.var_95_notional = portfolio_notional * loss_95;
        est.var_99_notional = portfolio_notional * loss_99;

        // Compute average loss in tail (CVaR 99%)
        double tail_sum = 0.0;
        size_t tail_count = std::max<size_t>(1, idx_99 + 1);
        for (size_t i = 0; i < tail_count; ++i) {
            tail_sum += (sorted_returns[i] < 0.0) ? -sorted_returns[i] : 0.0;
        }
        est.expected_shortfall = portfolio_notional * (tail_sum / tail_count);

        return est;
    }

    // Stress testing portfolio under severe market moves
    static double stress_test_pnl(
        double portfolio_delta_notional,
        double market_shock_pct) noexcept
    {
        return portfolio_delta_notional * (market_shock_pct / 100.0);
    }
};

} // namespace risk
} // namespace luv
