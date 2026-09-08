#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace margin {

struct MarginConfig {
    double initial_margin_pct = 10.0;     // 10% Initial Margin (10x max leverage)
    double maintenance_margin_pct = 5.0;  // 5% Maintenance Margin (Liquidation threshold)
};

enum class MarginCallStatus : uint8_t {
    kHealthy = 0,
    kWarning = 1,        // Equity < Initial Margin (No new positions allowed)
    kLiquidation = 2     // Equity <= Maintenance Margin (Immediate liquidation required)
};

struct AccountMarginState {
    int64_t cash_balance = 0;
    int64_t gross_position_notional = 0;
    int64_t unrealized_pnl = 0;
    int64_t equity = 0;
    int64_t initial_margin_required = 0;
    int64_t maintenance_margin_required = 0;
    double margin_ratio_pct = 0.0;
    MarginCallStatus status = MarginCallStatus::kHealthy;
};

class CrossMarginEngine {
public:
    explicit CrossMarginEngine(const MarginConfig& config = MarginConfig{}) noexcept
        : config_(config) {}

    // Evaluates margin status given cash balance, position size, entry price, and current market price
    AccountMarginState evaluate_account(
        int64_t cash_balance,
        uint8_t position_side,
        int64_t position_qty,
        int64_t entry_price,
        int64_t current_market_price) const noexcept
    {
        AccountMarginState state;
        state.cash_balance = cash_balance;

        if (position_qty <= 0 || current_market_price <= 0) {
            state.equity = cash_balance;
            state.status = MarginCallStatus::kHealthy;
            return state;
        }

        // 1. Position Notional = (Qty * Current Market Price) / 10000 (scaled)
        state.gross_position_notional = (position_qty * current_market_price) / 10000;

        // 2. Unrealized PnL
        if (position_side == exec::kBuy) {
            state.unrealized_pnl = (position_qty * (current_market_price - entry_price)) / 10000;
        } else {
            state.unrealized_pnl = (position_qty * (entry_price - current_market_price)) / 10000;
        }

        // 3. Equity = Cash + Unrealized PnL
        state.equity = state.cash_balance + state.unrealized_pnl;

        // 4. Margin Requirements
        state.initial_margin_required = static_cast<int64_t>(
            state.gross_position_notional * (config_.initial_margin_pct / 100.0));
        state.maintenance_margin_required = static_cast<int64_t>(
            state.gross_position_notional * (config_.maintenance_margin_pct / 100.0));

        // 5. Margin Ratio = Maintenance Margin / Equity
        if (state.equity > 0) {
            state.margin_ratio_pct = (static_cast<double>(state.maintenance_margin_required) / static_cast<double>(state.equity)) * 100.0;
        } else {
            state.margin_ratio_pct = 999.9; // Insolvent
        }

        // 6. Status Determination
        if (state.equity <= state.maintenance_margin_required) {
            state.status = MarginCallStatus::kLiquidation;
        } else if (state.equity < state.initial_margin_required) {
            state.status = MarginCallStatus::kWarning;
        } else {
            state.status = MarginCallStatus::kHealthy;
        }

        return state;
    }

    // Generates market liquidation order parameters to close open position
    static bool create_liquidation_order(
        uint8_t current_position_side,
        int64_t position_qty,
        uint8_t& out_liquidation_side,
        int64_t& out_liquidation_qty) noexcept
    {
        if (position_qty <= 0) return false;

        // Opposite side to close
        out_liquidation_side = (current_position_side == exec::kBuy) ? exec::kSell : exec::kBuy;
        out_liquidation_qty = position_qty;
        return true;
    }

private:
    MarginConfig config_;
};

} // namespace margin
} // namespace luv
