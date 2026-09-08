#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace luv {

struct GammaScalpParams {
    double delta_rebalance_threshold{0.10}; // Rebalance underlying when delta drift exceeds +/- 0.10
    double fee_per_share{0.005};            // $0.005 per share transaction fee
};

struct GammaScalpState {
    int64_t underlying_shares{0};
    double portfolio_options_delta{0.0};
    double net_portfolio_delta{0.0}; // underlying_shares + options_delta
    double total_realized_gamma_pnl{0.0};
    double total_transaction_costs{0.0};
    uint64_t rebalance_count{0};
    double last_rebalance_price{0.0};
};

class GammaScalpingController {
public:
    explicit GammaScalpingController(const GammaScalpParams& params = {}) noexcept
        : params_(params) {}

    // Initialize with starting options delta and spot price
    void initialize(double options_delta, double initial_spot) noexcept {
        state_.portfolio_options_delta = options_delta;
        state_.last_rebalance_price = initial_spot;

        // Perfect initial hedge: underlying_shares = -round(options_delta)
        state_.underlying_shares = -static_cast<int64_t>(std::round(options_delta));
        state_.net_portfolio_delta = static_cast<double>(state_.underlying_shares) + state_.portfolio_options_delta;
        state_.total_transaction_costs = std::abs(static_cast<double>(state_.underlying_shares)) * params_.fee_per_share;
    }

    // Process new market price and updated options delta
    bool on_market_update(double current_spot, double current_options_delta) noexcept {
        state_.portfolio_options_delta = current_options_delta;
        state_.net_portfolio_delta = static_cast<double>(state_.underlying_shares) + state_.portfolio_options_delta;

        // Check if net delta drift exceeds tolerance threshold
        if (std::abs(state_.net_portfolio_delta) >= params_.delta_rebalance_threshold) {
            // Need to trade underlying to return net delta to zero
            int64_t target_underlying = -static_cast<int64_t>(std::round(current_options_delta));
            int64_t shares_to_trade = target_underlying - state_.underlying_shares;

            if (shares_to_trade != 0) {
                // Realized PnL from stock trading against move
                double price_change = current_spot - state_.last_rebalance_price;
                double trade_pnl = static_cast<double>(state_.underlying_shares) * price_change;
                state_.total_realized_gamma_pnl += trade_pnl;

                // Costs
                state_.total_transaction_costs += std::abs(static_cast<double>(shares_to_trade)) * params_.fee_per_share;

                state_.underlying_shares = target_underlying;
                state_.net_portfolio_delta = static_cast<double>(state_.underlying_shares) + state_.portfolio_options_delta;
                state_.last_rebalance_price = current_spot;
                ++state_.rebalance_count;
                return true; // Rebalance executed
            }
        }

        return false;
    }

    const GammaScalpState& get_state() const noexcept { return state_; }

private:
    GammaScalpParams params_;
    GammaScalpState state_{};
};

} // namespace luv
