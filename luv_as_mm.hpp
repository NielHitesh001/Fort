#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace luv {
namespace mm {

struct AvellanedaStoikovConfig {
    double gamma = 0.1;           // Inventory risk aversion parameter
    double sigma = 0.02;          // Daily asset price volatility
    double time_horizon = 1.0;    // Remaining time horizon (fraction of day)
    double kappa = 1.5;           // Order arrival book liquidity intensity parameter
    int64_t min_spread_ticks = 1; // Minimum spread in ticks
    int64_t tick_size = 1;        // Tick increment
};

struct OptimalQuote {
    int64_t bid_price = 0;
    int64_t ask_price = 0;
    int64_t reservation_price = 0;
    int64_t spread_ticks = 0;
};

class AvellanedaStoikovQuoter {
public:
    explicit AvellanedaStoikovQuoter(const AvellanedaStoikovConfig& config = AvellanedaStoikovConfig{}) noexcept
        : config_(config) {}

    // Computes optimal two-sided quote around mid-price given inventory position q
    OptimalQuote compute_quotes(
        int64_t mid_price,
        int64_t inventory_q) const noexcept
    {
        OptimalQuote quote;
        if (mid_price <= 0) return quote;

        const double s = static_cast<double>(mid_price);
        const double q = static_cast<double>(inventory_q);
        const double gamma = config_.gamma;
        const double sigma = config_.sigma;
        const double tte = config_.time_horizon;
        const double kappa = config_.kappa;

        // 1. Reservation Price: r(s, q) = s - q * gamma * sigma^2 * T
        const double r = s - (q * gamma * sigma * sigma * tte);
        quote.reservation_price = static_cast<int64_t>(std::round(r));

        // 2. Optimal Spread: spread = (2 / gamma) * ln(1 + gamma / kappa)
        const double optimal_spread = (2.0 / gamma) * std::log(1.0 + (gamma / kappa));
        const double half_spread = optimal_spread / 2.0;

        double calc_bid = r - half_spread;
        double calc_ask = r + half_spread;

        // Convert to integer tick prices
        int64_t bid = static_cast<int64_t>(std::floor(calc_bid));
        int64_t ask = static_cast<int64_t>(std::ceil(calc_ask));

        // Enforce minimum spread constraint
        if (ask <= bid) {
            ask = bid + config_.min_spread_ticks * config_.tick_size;
        }

        quote.bid_price = bid;
        quote.ask_price = ask;
        quote.spread_ticks = (ask - bid) / config_.tick_size;

        return quote;
    }

private:
    AvellanedaStoikovConfig config_;
};

} // namespace mm
} // namespace luv
