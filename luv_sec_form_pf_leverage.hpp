#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <array>
#include <algorithm>

namespace luv {

struct FormPFFundData {
    char fund_id[16]{0};
    double net_asset_value{0.0};           // NAV
    double gross_notional_exposure{0.0};   // GNE (Long Notional + Short Notional)
    double net_notional_exposure{0.0};     // NNE (Long Notional - Short Notional)
    double secured_borrowings{0.0};        // Prime brokerage / repo secured debt
    double unsecured_borrowings{0.0};      // Unsecured borrowings
    double unencumbered_cash{0.0};         // Free cash not pledged as collateral
    double collateral_pledged{0.0};        // Total collateral posted
    double collateral_rehypothecated{0.0}; // Amount of collateral rehypothecated by broker
    double daily_turnover_usd{0.0};        // Daily trading volume
};

struct FormPFLeverageMetrics {
    double gross_leverage_ratio{0.0};      // GNE / NAV
    double net_leverage_ratio{0.0};        // NNE / NAV
    double borrowing_to_nav_ratio{0.0};    // (Secured + Unsecured Debt) / NAV
    double unencumbered_cash_ratio{0.0};   // Unencumbered Cash / NAV
    double rehypothecation_pct{0.0};       // Rehypothecated / Total Collateral (%)
    double monthly_turnover_rate_pct{0.0}; // Monthly turnover / NAV (%)
    bool qualifying_hedge_fund{false};     // Net assets >= $500M (Large Hedge Fund threshold)
    bool leverage_alert{false};            // Gross leverage > 5.0x or Borrowing > 2.0x
};

class FormPFLeverageEngine {
public:
    static constexpr double LARGE_HEDGE_FUND_THRESHOLD = 500'000'000.0; // $500M NAV
    static constexpr double MAX_SAFE_GROSS_LEVERAGE = 5.0;              // 5.0x GNE/NAV
    static constexpr double MAX_SAFE_BORROWING_LEVERAGE = 2.0;          // 2.0x Debt/NAV

    static FormPFLeverageMetrics calculate_metrics(const FormPFFundData& fund) noexcept {
        FormPFLeverageMetrics metrics{};
        if (fund.net_asset_value <= 0.0) {
            return metrics;
        }

        double nav = fund.net_asset_value;
        metrics.gross_leverage_ratio = fund.gross_notional_exposure / nav;
        metrics.net_leverage_ratio = fund.net_notional_exposure / nav;

        double total_borrowings = fund.secured_borrowings + fund.unsecured_borrowings;
        metrics.borrowing_to_nav_ratio = total_borrowings / nav;
        metrics.unencumbered_cash_ratio = fund.unencumbered_cash / nav;

        if (fund.collateral_pledged > 0.0) {
            metrics.rehypothecation_pct = (fund.collateral_rehypothecated / fund.collateral_pledged) * 100.0;
        }

        // Monthly turnover assumed 21 trading days
        double monthly_volume = fund.daily_turnover_usd * 21.0;
        metrics.monthly_turnover_rate_pct = (monthly_volume / nav) * 100.0;

        metrics.qualifying_hedge_fund = (nav >= LARGE_HEDGE_FUND_THRESHOLD);
        metrics.leverage_alert = (metrics.gross_leverage_ratio > MAX_SAFE_GROSS_LEVERAGE) ||
                                 (metrics.borrowing_to_nav_ratio > MAX_SAFE_BORROWING_LEVERAGE);

        return metrics;
    }
};

} // namespace luv
