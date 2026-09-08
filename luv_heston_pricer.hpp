#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <complex>
#include <algorithm>

namespace luv {

struct HestonParameters {
    double spot_price{100.0};       // S0
    double strike_price{100.0};     // K
    double risk_free_rate{0.03};    // r
    double time_to_maturity{1.0};   // T (years)
    double initial_variance{0.04};  // v0 (sigma0 = 20%)
    double mean_reversion_kappa{2.0}; // kappa
    double long_term_var_theta{0.04}; // theta (long term vol = 20%)
    double vol_of_vol_xi{0.30};     // xi / sigma_v
    double correlation_rho{-0.70};  // rho (leverage effect)
};

struct HestonPriceResult {
    double call_price{0.0};
    double put_price{0.0};
    double implied_volatility_approx{0.0};
    bool valid{false};
};

class HestonOptionPricer {
public:
    static constexpr size_t QUADRATURE_STEPS = 64;
    static constexpr double INTEGRATION_UPPER_BOUND = 80.0;

    static HestonPriceResult price_european_option(const HestonParameters& params) noexcept {
        HestonPriceResult result{};
        if (params.spot_price <= 0.0 || params.strike_price <= 0.0 || params.time_to_maturity <= 0.0) {
            return result;
        }

        double s0 = params.spot_price;
        double k = params.strike_price;
        double r = params.risk_free_rate;
        double t = params.time_to_maturity;

        double p1 = compute_prob(1, params);
        double p2 = compute_prob(2, params);

        double call = s0 * p1 - k * std::exp(-r * t) * p2;
        call = std::max(0.0, std::max(call, s0 - k * std::exp(-r * t))); // Intrinsic floor

        // Put price by Put-Call Parity: P = C - S0 + K e^{-r T}
        double put = call - s0 + (k * std::exp(-r * t));
        put = std::max(0.0, put);

        result.call_price = call;
        result.put_price = put;
        result.implied_volatility_approx = std::sqrt(params.initial_variance);
        result.valid = true;
        return result;
    }

private:
    using Complex = std::complex<double>;

    static Complex heston_cf(int j, double phi, const HestonParameters& p) noexcept {
        Complex i(0.0, 1.0);
        double s0 = p.spot_price;
        double r = p.risk_free_rate;
        double t = p.time_to_maturity;
        double v0 = p.initial_variance;
        double kappa = p.mean_reversion_kappa;
        double theta = p.long_term_var_theta;
        double xi = p.vol_of_vol_xi;
        double rho = p.correlation_rho;

        double u = (j == 1) ? 0.5 : -0.5;
        double b = (j == 1) ? (kappa - rho * xi) : kappa;

        Complex term1 = b - rho * xi * phi * i;
        Complex d = std::sqrt(term1 * term1 - (xi * xi) * (2.0 * u * phi * i - phi * phi));
        Complex g = (b - rho * xi * phi * i + d) / (b - rho * xi * phi * i - d);

        Complex exp_dt = std::exp(d * t);
        Complex c_term = r * phi * i * t + (kappa * theta / (xi * xi)) *
            ((b - rho * xi * phi * i + d) * t - 2.0 * std::log((1.0 - g * exp_dt) / (1.0 - g)));
        Complex d_term = ((b - rho * xi * phi * i + d) / (xi * xi)) *
            ((1.0 - exp_dt) / (1.0 - g * exp_dt));

        return std::exp(c_term + d_term * v0 + i * phi * std::log(s0));
    }

    static double compute_prob(int j, const HestonParameters& params) noexcept {
        double k = params.strike_price;
        double log_k = std::log(k);
        Complex i(0.0, 1.0);

        // Trapezoidal / Simpson quadrature integration over [0, INTEGRATION_UPPER_BOUND]
        double h = INTEGRATION_UPPER_BOUND / static_cast<double>(QUADRATURE_STEPS);
        double integral_sum = 0.0;

        for (size_t step = 1; step <= QUADRATURE_STEPS; ++step) {
            double phi = (static_cast<double>(step) - 0.5) * h;
            Complex cf = heston_cf(j, phi, params);
            Complex integrand = std::exp(-i * phi * log_k) * cf / (i * phi);
            integral_sum += integrand.real() * h;
        }

        double prob = 0.5 + (1.0 / 3.14159265358979323846) * integral_sum;
        return std::clamp(prob, 0.0, 1.0);
    }
};

} // namespace luv
