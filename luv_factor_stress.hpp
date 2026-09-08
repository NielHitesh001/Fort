#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace risk {

enum class MacroFactor : uint8_t {
    kEquityIndexSPX = 0,
    kInterestRate10Y = 1,
    kCrudeOilWTI = 2,
    kFxEURUSD = 3,
    kVolatilityVIX = 4
};

struct AssetFactorSensitivities {
    uint16_t symbol_idx = 0;
    int64_t position_notional = 0; // Scaled x10,000 (dollar notional)
    double equity_beta = 1.0;
    double interest_rate_dv01 = 0.0;
    double commodity_beta = 0.0;
    double fx_delta = 0.0;
    double vega = 0.0;
};

struct MacroStressScenario {
    double spx_shock_pct = 0.0;     // e.g. -0.10 (-10%)
    double rates_shock_bps = 0.0;   // e.g. +100.0 (+100 bps)
    double oil_shock_pct = 0.0;     // e.g. -0.20 (-20%)
    double fx_shock_pct = 0.0;      // e.g. +0.05 (+5%)
    double vix_shock_pct = 0.0;     // e.g. +0.50 (+50%)
};

struct StressTestResult {
    int64_t initial_portfolio_val = 0;
    int64_t projected_pnl = 0; // Projected dollar PnL under macro shock
    double projected_drawdown_pct = 0.0;
    bool margin_call_triggered = false;
};

class MacroFactorStressEngine {
public:
    static constexpr size_t kMaxPositions = 64;

    MacroFactorStressEngine() noexcept : num_positions_(0) {}

    bool register_position_factors(const AssetFactorSensitivities& sens) noexcept {
        if (num_positions_ >= kMaxPositions) return false;
        positions_[num_positions_++] = sens;
        return true;
    }

    // Projects total portfolio loss under a specific macro factor stress scenario
    StressTestResult evaluate_stress_scenario(const MacroStressScenario& scenario, int64_t portfolio_equity) const noexcept {
        StressTestResult res{};
        res.initial_portfolio_val = portfolio_equity;

        double total_pnl_dollars = 0.0;

        for (size_t i = 0; i < num_positions_; ++i) {
            const auto& pos = positions_[i];
            double notional = static_cast<double>(pos.position_notional) / 10000.0;

            // 1. Equity shock contribution
            double eq_pnl = notional * pos.equity_beta * scenario.spx_shock_pct;

            // 2. Interest rate shock contribution (DV01 in dollars per bp)
            double rate_pnl = pos.interest_rate_dv01 * scenario.rates_shock_bps;

            // 3. Commodity shock contribution
            double com_pnl = notional * pos.commodity_beta * scenario.oil_shock_pct;

            // 4. FX shock contribution
            double fx_pnl = notional * pos.fx_delta * scenario.fx_shock_pct;

            // 5. Volatility shock contribution
            double vol_pnl = pos.vega * (scenario.vix_shock_pct * 100.0);

            total_pnl_dollars += (eq_pnl + rate_pnl + com_pnl + fx_pnl + vol_pnl);
        }

        res.projected_pnl = static_cast<int64_t>(std::round(total_pnl_dollars * 10000.0));

        if (portfolio_equity > 0) {
            res.projected_drawdown_pct = (static_cast<double>(res.projected_pnl) / static_cast<double>(portfolio_equity)) * 100.0;
            // If drawdown exceeds -30%, trigger simulated stress margin call
            res.margin_call_triggered = (res.projected_drawdown_pct <= -30.0);
        }

        return res;
    }

private:
    std::array<AssetFactorSensitivities, kMaxPositions> positions_{};
    size_t num_positions_{0};
};

} // namespace risk
} // namespace luv
