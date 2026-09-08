#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct RollSpreadResult {
    double autocovariance{0.0};
    double effective_spread{0.0};
    double effective_half_spread{0.0};
    size_t sample_count{0};
};

struct GlostenMilgromQuote {
    double bid_price{0.0};
    double ask_price{0.0};
    double mid_price{0.0};
    double adverse_selection_spread{0.0};
    double current_p_high{0.5}; // Posterior belief of high fundamental value
};

struct GlostenMilgromParams {
    double v_high{105.0};        // Fundamental high valuation
    double v_low{95.0};          // Fundamental low valuation
    double alpha_informed{0.20}; // Proportion of informed traders (alpha in [0, 1])
    double initial_p_high{0.50}; // Initial prior belief P(V = V_H)
};

class RollSpreadEstimator {
public:
    static constexpr size_t MAX_SAMPLES = 512;

    RollSpreadEstimator() noexcept {
        reset();
    }

    void reset() noexcept {
        price_count_ = 0;
    }

    bool add_price(double price) noexcept {
        if (price_count_ >= MAX_SAMPLES) {
            // Shift left to maintain rolling window
            for (size_t i = 1; i < MAX_SAMPLES; ++i) {
                prices_[i - 1] = prices_[i];
            }
            prices_[MAX_SAMPLES - 1] = price;
            return true;
        }
        prices_[price_count_++] = price;
        return true;
    }

    RollSpreadResult compute_roll_spread() const noexcept {
        RollSpreadResult result{};
        if (price_count_ < 3) return result;

        size_t n = price_count_ - 1; // Number of price changes
        std::array<double, MAX_SAMPLES> dp{};
        double sum_dp = 0.0;
        for (size_t i = 0; i < n; ++i) {
            dp[i] = prices_[i + 1] - prices_[i];
            sum_dp += dp[i];
        }
        double mean_dp = sum_dp / static_cast<double>(n);

        // Compute lag-1 autocovariance: Cov(dp_t, dp_{t-1})
        double cov_sum = 0.0;
        size_t cov_count = n - 1;
        for (size_t i = 1; i < n; ++i) {
            cov_sum += (dp[i] - mean_dp) * (dp[i - 1] - mean_dp);
        }
        double gamma1 = (cov_count > 0) ? (cov_sum / static_cast<double>(cov_count)) : 0.0;

        result.autocovariance = gamma1;
        result.sample_count = price_count_;

        if (gamma1 < 0.0) {
            result.effective_spread = 2.0 * std::sqrt(-gamma1);
            result.effective_half_spread = std::sqrt(-gamma1);
        } else {
            result.effective_spread = 0.0;
            result.effective_half_spread = 0.0;
        }

        return result;
    }

    size_t sample_count() const noexcept { return price_count_; }

private:
    std::array<double, MAX_SAMPLES> prices_{};
    size_t price_count_{0};
};

class GlostenMilgromEngine {
public:
    explicit GlostenMilgromEngine(const GlostenMilgromParams& params = GlostenMilgromParams{}) noexcept
        : params_(params), p_high_(params.initial_p_high) {}

    void reset() noexcept {
        p_high_ = params_.initial_p_high;
    }

    GlostenMilgromQuote get_quotes() const noexcept {
        GlostenMilgromQuote quote{};
        double p = p_high_;
        double alpha = params_.alpha_informed;
        double vh = params_.v_high;
        double vl = params_.v_low;

        // Ask price: E[V | Buy]
        double p_buy = 0.5 * (1.0 + alpha * (2.0 * p - 1.0));
        double ask = (p_buy > 1e-9) ? ((p * vh * 0.5 * (1.0 + alpha) + (1.0 - p) * vl * 0.5 * (1.0 - alpha)) / p_buy) : ((vh + vl) / 2.0);

        // Bid price: E[V | Sell]
        double p_sell = 0.5 * (1.0 - alpha * (2.0 * p - 1.0));
        double bid = (p_sell > 1e-9) ? ((p * vh * 0.5 * (1.0 - alpha) + (1.0 - p) * vl * 0.5 * (1.0 + alpha)) / p_sell) : ((vh + vl) / 2.0);

        quote.ask_price = ask;
        quote.bid_price = bid;
        quote.mid_price = (ask + bid) / 2.0;
        quote.adverse_selection_spread = ask - bid;
        quote.current_p_high = p_high_;
        return quote;
    }

    // Process incoming order: is_buy = true for Buy order, false for Sell order
    void process_trade(bool is_buy) noexcept {
        double p = p_high_;
        double alpha = params_.alpha_informed;

        if (is_buy) {
            // Posterior after observing Buy: P(V_H | Buy) = p * (1 + alpha) / (1 + alpha * (2p - 1))
            double denom = 1.0 + alpha * (2.0 * p - 1.0);
            if (denom > 1e-9) {
                p_high_ = (p * (1.0 + alpha)) / denom;
            }
        } else {
            // Posterior after observing Sell: P(V_H | Sell) = p * (1 - alpha) / (1 - alpha * (2p - 1))
            double denom = 1.0 - alpha * (2.0 * p - 1.0);
            if (denom > 1e-9) {
                p_high_ = (p * (1.0 - alpha)) / denom;
            }
        }

        // Clamp to prevent floating precision instability
        p_high_ = std::clamp(p_high_, 0.001, 0.999);
    }

    double current_p_high() const noexcept { return p_high_; }

private:
    GlostenMilgromParams params_{};
    double p_high_{0.50};
};

} // namespace luv
