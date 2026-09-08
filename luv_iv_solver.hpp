#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace luv {

enum class OptionType : uint8_t {
    Call = 0,
    Put = 1
};

struct IvSolverParams {
    double spot{100.0};
    double strike{100.0};
    double rate{0.05};       // risk-free rate
    double time_to_expiry{1.0}; // in years
    OptionType type{OptionType::Call};
    double market_price{10.0};
    double tol{1e-6};
    uint32_t max_iter{50};
};

class IvSolver {
public:
    // Standard normal CDF approximation (Abramowitz & Stegun / Hart formula)
    static double normal_cdf(double x) noexcept {
        return 0.5 * std::erfc(-x * M_SQRT1_2);
    }

    // Standard normal PDF
    static double normal_pdf(double x) noexcept {
        constexpr double inv_sqrt_2pi = 0.398942280401432677939946059934;
        return inv_sqrt_2pi * std::exp(-0.5 * x * x);
    }

    // Black-Scholes price given volatility
    static double bs_price(double S, double K, double r, double T, double sigma, OptionType type) noexcept {
        if (T <= 0.0 || sigma <= 0.0) {
            if (type == OptionType::Call) return std::max(0.0, S - K);
            return std::max(0.0, K - S);
        }

        double sqrt_T = std::sqrt(T);
        double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T);
        double d2 = d1 - sigma * sqrt_T;

        double disc_K = K * std::exp(-r * T);

        if (type == OptionType::Call) {
            return S * normal_cdf(d1) - disc_K * normal_cdf(d2);
        } else {
            return disc_K * normal_cdf(-d2) - S * normal_cdf(-d1);
        }
    }

    // Vega derivative dPrice / dSigma
    static double bs_vega(double S, double K, double r, double T, double sigma) noexcept {
        if (T <= 0.0 || sigma <= 0.0) return 0.0;
        double sqrt_T = std::sqrt(T);
        double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T);
        return S * sqrt_T * normal_pdf(d1);
    }

    // Solve for implied volatility using Newton-Raphson with fallback to Bisection
    static double solve(const IvSolverParams& p) noexcept {
        if (p.market_price <= 0.0 || p.time_to_expiry <= 0.0) return 0.0;

        // Intrinsic value check
        double intrinsic = (p.type == OptionType::Call) ? 
            std::max(0.0, p.spot - p.strike * std::exp(-p.rate * p.time_to_expiry)) :
            std::max(0.0, p.strike * std::exp(-p.rate * p.time_to_expiry) - p.spot);

        if (p.market_price < intrinsic) return 0.0;

        // Initial guess (Brenner-Subrahmanyam / Corrado-Miller approx)
        double sigma = std::sqrt(2.0 * M_PI / p.time_to_expiry) * (p.market_price / p.spot);
        sigma = std::clamp(sigma, 0.01, 3.0);

        double low = 0.001;
        double high = 5.0;

        for (uint32_t i = 0; i < p.max_iter; ++i) {
            double price = bs_price(p.spot, p.strike, p.rate, p.time_to_expiry, sigma, p.type);
            double diff = price - p.market_price;

            if (std::abs(diff) < p.tol) {
                return sigma;
            }

            double vega = bs_vega(p.spot, p.strike, p.rate, p.time_to_expiry, sigma);

            if (vega > 1e-8) {
                double step = diff / vega;
                sigma -= step;
                if (sigma <= low || sigma >= high) {
                    // Fall back to bisection bracket
                    if (diff > 0) high = sigma + step;
                    else low = sigma + step;
                    sigma = 0.5 * (low + high);
                }
            } else {
                // Bisection step
                if (diff > 0) high = sigma;
                else low = sigma;
                sigma = 0.5 * (low + high);
            }
        }

        return sigma;
    }
};

} // namespace luv
