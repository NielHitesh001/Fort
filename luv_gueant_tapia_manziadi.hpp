#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace luv {

struct GTMParameters {
    double risk_aversion_gamma{0.1};  // Inventory risk aversion parameter gamma
    double order_flow_kappa{1.5};     // Liquidity parameter kappa (intensity decay)
    double volatility_sigma{0.02};    // Mid-price annual/daily volatility sigma
    double intensity_A{140.0};        // Base arrival intensity A
    double terminal_penalty_alpha{0.005}; // Terminal inventory liquidation penalty
    int64_t max_inventory{100};       // Hard inventory position boundary
    double min_tick_size{0.01};
};

struct GTMQuotes {
    double mid_price{0.0};
    int64_t current_inventory{0};
    double delta_bid{0.0};
    double delta_ask{0.0};
    double optimal_bid_price{0.0};
    double optimal_ask_price{0.0};
    double reservation_price{0.0};
    double expected_half_spread{0.0};
    bool quotes_active{true};
};

class GueantTapiaManziadiEngine {
public:
    explicit GueantTapiaManziadiEngine(const GTMParameters& params = GTMParameters{}) noexcept
        : params_(params) {
        precompute_constants();
    }

    void set_parameters(const GTMParameters& params) noexcept {
        params_ = params;
        precompute_constants();
    }

    GTMQuotes calculate_quotes(double mid_price, int64_t inventory_q) const noexcept {
        GTMQuotes quotes{};
        quotes.mid_price = mid_price;
        quotes.current_inventory = inventory_q;

        if (std::abs(inventory_q) >= params_.max_inventory) {
            // Skew entirely or halt one side to prevent boundary breach
            if (inventory_q >= params_.max_inventory) {
                // Saturated long: do not bid, quote only ask
                quotes.delta_ask = params_.min_tick_size;
                quotes.optimal_ask_price = std::round((mid_price + quotes.delta_ask) / params_.min_tick_size) * params_.min_tick_size;
                quotes.delta_bid = 999999.0;
                quotes.optimal_bid_price = 0.0;
                quotes.quotes_active = true;
                return quotes;
            } else {
                // Saturated short: do not ask, quote only bid
                quotes.delta_bid = params_.min_tick_size;
                quotes.optimal_bid_price = std::round((mid_price - quotes.delta_bid) / params_.min_tick_size) * params_.min_tick_size;
                quotes.delta_ask = 999999.0;
                quotes.optimal_ask_price = 0.0;
                quotes.quotes_active = true;
                return quotes;
            }
        }

        double q = static_cast<double>(inventory_q);

        // Guéant-Tapia-Manziadi closed-form spread terms:
        // When q > 0 (long): delta_ask decreases (ask closer), delta_bid increases (bid deeper)
        // delta_a*(q) = base_spread_term - (2q - 1)/2 * skew_term
        // delta_b*(q) = base_spread_term + (2q + 1)/2 * skew_term
        double delta_ask = base_spread_term_ - ((2.0 * q - 1.0) / 2.0) * skew_term_;
        double delta_bid = base_spread_term_ + ((2.0 * q + 1.0) / 2.0) * skew_term_;

        // Floor at minimum tick size
        delta_ask = std::max(params_.min_tick_size, delta_ask);
        delta_bid = std::max(params_.min_tick_size, delta_bid);

        quotes.delta_ask = delta_ask;
        quotes.delta_bid = delta_bid;
        quotes.optimal_bid_price = std::round((mid_price - delta_bid) / params_.min_tick_size) * params_.min_tick_size;
        quotes.optimal_ask_price = std::round((mid_price + delta_ask) / params_.min_tick_size) * params_.min_tick_size;

        // Reservation price: R(s, q) = s - q * gamma * sigma^2
        quotes.reservation_price = mid_price - (q * params_.risk_aversion_gamma * params_.volatility_sigma * params_.volatility_sigma);
        quotes.expected_half_spread = (delta_ask + delta_bid) / 2.0;
        quotes.quotes_active = true;

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

        // base_spread_term = (1 / gamma) * ln(1 + gamma / kappa)
        base_spread_term_ = (1.0 / gamma) * std::log(1.0 + (gamma / kappa));

        // skew_term = sqrt( (gamma * sigma^2) / (2 * kappa) * (1 + gamma / kappa)^(1 + kappa / gamma) )
        double power_base = 1.0 + (gamma / kappa);
        double exponent = 1.0 + (kappa / gamma);
        double factor = (gamma * sigma * sigma) / (2.0 * kappa);
        skew_term_ = std::sqrt(std::max(0.0, factor * std::pow(power_base, exponent)));
    }

    GTMParameters params_{};
    double base_spread_term_{0.0};
    double skew_term_{0.0};
};

} // namespace luv
