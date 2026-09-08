#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace luv {

struct CarteaJaimungalParams {
    double risk_aversion_gamma{0.08}; // Inventory risk aversion parameter
    double order_flow_kappa{1.5};     // Liquidity decay parameter
    double volatility_sigma{0.02};    // Mid-price volatility
    double alpha_sensitivity{1.0};    // Multiplier for alpha signal drift
    int64_t max_inventory{100};       // Hard inventory bound
    double min_tick_size{0.01};
};

struct CarteaJaimungalQuotes {
    double mid_price{0.0};
    int64_t current_inventory{0};
    double alpha_signal{0.0};
    double delta_bid{0.0};
    double delta_ask{0.0};
    double optimal_bid_price{0.0};
    double optimal_ask_price{0.0};
    double quote_center_shift{0.0};
    double expected_half_spread{0.0};
    bool quotes_active{true};
};

class CarteaJaimungalEngine {
public:
    explicit CarteaJaimungalEngine(const CarteaJaimungalParams& params = CarteaJaimungalParams{}) noexcept
        : params_(params) {
        precompute_constants();
    }

    void set_parameters(const CarteaJaimungalParams& params) noexcept {
        params_ = params;
        precompute_constants();
    }

    CarteaJaimungalQuotes calculate_quotes(double mid_price, int64_t inventory_q, double alpha_drift) const noexcept {
        CarteaJaimungalQuotes quotes{};
        quotes.mid_price = mid_price;
        quotes.current_inventory = inventory_q;
        quotes.alpha_signal = alpha_drift;

        if (std::abs(inventory_q) >= params_.max_inventory) {
            if (inventory_q >= params_.max_inventory) {
                // Saturated long: do not bid, aggressively quote ask
                quotes.delta_ask = params_.min_tick_size;
                quotes.optimal_ask_price = std::round((mid_price + quotes.delta_ask) / params_.min_tick_size) * params_.min_tick_size;
                quotes.delta_bid = 999999.0;
                quotes.optimal_bid_price = 0.0;
                return quotes;
            } else {
                // Saturated short: do not ask, aggressively quote bid
                quotes.delta_bid = params_.min_tick_size;
                quotes.optimal_bid_price = std::round((mid_price - quotes.delta_bid) / params_.min_tick_size) * params_.min_tick_size;
                quotes.delta_ask = 999999.0;
                quotes.optimal_ask_price = 0.0;
                return quotes;
            }
        }

        double q = static_cast<double>(inventory_q);

        // Base inventory skew terms from GTM:
        double delta_ask_base = base_spread_term_ - ((2.0 * q - 1.0) / 2.0) * skew_term_;
        double delta_bid_base = base_spread_term_ + ((2.0 * q + 1.0) / 2.0) * skew_term_;

        // Cartea-Jaimungal Alpha Shift:
        // When alpha > 0 (bullish), shift entire quotes up:
        // P_ask = S + delta_ask -> delta_ask = delta_ask_base + (alpha / kappa)
        // P_bid = S - delta_bid -> delta_bid = delta_bid_base - (alpha / kappa)
        double alpha_shift = (params_.alpha_sensitivity * alpha_drift) / params_.order_flow_kappa;
        quotes.quote_center_shift = alpha_shift;

        double delta_ask = delta_ask_base + alpha_shift;
        double delta_bid = delta_bid_base - alpha_shift;

        // Floor at minimum tick size
        delta_ask = std::max(params_.min_tick_size, delta_ask);
        delta_bid = std::max(params_.min_tick_size, delta_bid);

        quotes.delta_ask = delta_ask;
        quotes.delta_bid = delta_bid;
        quotes.optimal_bid_price = std::round((mid_price - delta_bid) / params_.min_tick_size) * params_.min_tick_size;
        quotes.optimal_ask_price = std::round((mid_price + delta_ask) / params_.min_tick_size) * params_.min_tick_size;
        quotes.expected_half_spread = (delta_ask + delta_bid) / 2.0;

        return quotes;
    }

private:
    void precompute_constants() noexcept {
        double gamma = params_.risk_aversion_gamma;
        double kappa = params_.order_flow_kappa;
        double sigma = params_.volatility_sigma;

        if (gamma <= 0.0 || kappa <= 0.0) {
            base_spread_term_ = params_.min_tick_size;
            skew_term_ = 0.0;
            return;
        }

        base_spread_term_ = (1.0 / gamma) * std::log(1.0 + (gamma / kappa));

        double power_base = 1.0 + (gamma / kappa);
        double exponent = 1.0 + (kappa / gamma);
        double factor = (gamma * sigma * sigma) / (2.0 * kappa);
        skew_term_ = std::sqrt(std::max(0.0, factor * std::pow(power_base, exponent)));
    }

    CarteaJaimungalParams params_{};
    double base_spread_term_{0.0};
    double skew_term_{0.0};
};

} // namespace luv
