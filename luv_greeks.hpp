#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace luv {
namespace greeks {

enum class OptionType : uint8_t {
    kCall = 0,
    kPut = 1
};

struct OptionGreeks {
    double price = 0.0;
    double delta = 0.0;
    double gamma = 0.0;
    double vega = 0.0;
    double theta = 0.0;
};

class BlackScholesEngine {
public:
    // Standard normal cumulative distribution function (Abramowitz & Stegun approximation)
    static double normal_cdf(double x) noexcept {
        return 0.5 * std::erfc(-x * M_SQRT1_2);
    }

    // Standard normal probability density function
    static double normal_pdf(double x) noexcept {
        return (1.0 / std::sqrt(2.0 * M_PI)) * std::exp(-0.5 * x * x);
    }

    // Calculates price and all primary Greeks
    static OptionGreeks compute_greeks(
        OptionType type,
        double spot,        // Underlying spot price
        double strike,      // Option strike price
        double time_to_exp, // Time to expiration in years (e.g. 30/365)
        double risk_free_r, // Annual risk-free interest rate (e.g. 0.05)
        double vol) noexcept// Implied volatility (e.g. 0.20)
    {
        OptionGreeks g;
        if (spot <= 0.0 || strike <= 0.0 || time_to_exp <= 0.0 || vol <= 0.0) {
            return g;
        }

        const double sqrt_t = std::sqrt(time_to_exp);
        const double d1 = (std::log(spot / strike) + (risk_free_r + 0.5 * vol * vol) * time_to_exp) / (vol * sqrt_t);
        const double d2 = d1 - vol * sqrt_t;

        const double pdf_d1 = normal_pdf(d1);
        const double disc = std::exp(-risk_free_r * time_to_exp);

        if (type == OptionType::kCall) {
            g.price = spot * normal_cdf(d1) - strike * disc * normal_cdf(d2);
            g.delta = normal_cdf(d1);
            g.theta = (-spot * pdf_d1 * vol / (2.0 * sqrt_t) - risk_free_r * strike * disc * normal_cdf(d2)) / 365.0;
        } else {
            g.price = strike * disc * normal_cdf(-d2) - spot * normal_cdf(-d1);
            g.delta = normal_cdf(d1) - 1.0;
            g.theta = (-spot * pdf_d1 * vol / (2.0 * sqrt_t) + risk_free_r * strike * disc * normal_cdf(-d2)) / 365.0;
        }

        g.gamma = pdf_d1 / (spot * vol * sqrt_t);
        g.vega = (spot * sqrt_t * pdf_d1) / 100.0; // Per 1% change in vol

        return g;
    }
};

// Portfolio Delta & Gamma Aggregator
struct PortfolioRiskLimits {
    double max_net_delta = 500.0;  // Max +/- 500 delta shares
    double max_gross_gamma = 100.0; // Max 100 gamma
};

class OptionsPortfolioRisk {
public:
    explicit OptionsPortfolioRisk(const PortfolioRiskLimits& limits) noexcept : limits_(limits) {}

    void add_position(OptionType type, double spot, double strike, double tte, double r, double vol, int64_t contracts) noexcept {
        auto g = BlackScholesEngine::compute_greeks(type, spot, strike, tte, r, vol);
        net_delta_ += g.delta * 100.0 * static_cast<double>(contracts);
        gross_gamma_ += std::abs(g.gamma * 100.0 * static_cast<double>(contracts));
    }

    bool check_risk(bool& out_delta_breach, bool& out_gamma_breach) const noexcept {
        out_delta_breach = (std::abs(net_delta_) > limits_.max_net_delta);
        out_gamma_breach = (gross_gamma_ > limits_.max_gross_gamma);
        return (!out_delta_breach && !out_gamma_breach);
    }

    double net_delta() const noexcept { return net_delta_; }
    double gross_gamma() const noexcept { return gross_gamma_; }

    void reset() noexcept {
        net_delta_ = 0.0;
        gross_gamma_ = 0.0;
    }

private:
    PortfolioRiskLimits limits_;
    double net_delta_{0.0};
    double gross_gamma_{0.0};
};

} // namespace greeks
} // namespace luv
