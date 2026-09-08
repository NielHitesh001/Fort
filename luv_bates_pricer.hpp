#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <complex>
#include <algorithm>

namespace luv {

struct BatesParameters {
    double spot_price{100.0};           // S0
    double strike_price{100.0};         // K
    double risk_free_rate{0.03};        // r
    double dividend_yield{0.0};         // q
    double time_to_maturity{1.0};       // T (years)
    double initial_variance{0.04};      // V0 (v0 = sigma0^2)
    double mean_reversion_kappa{2.0};   // kappa
    double long_term_var_theta{0.04};   // theta
    double vol_of_vol_xi{0.30};         // xi (vol of variance)
    double correlation_rho{-0.70};      // rho (correlation between asset and vol Brownian motions)
    double jump_intensity_lambda{0.10}; // lambda (expected jumps per year)
    double jump_mean_gamma{-0.05};      // gamma (mean of log jump size)
    double jump_vol_delta{0.15};        // delta (std dev of log jump size)
};

struct BatesGreeks {
    double delta{0.0};
    double gamma{0.0};
    double vega{0.0};
    double theta{0.0};
    double rho{0.0};
    double jump_lambda_sensitivity{0.0};
};

struct BatesPriceResult {
    double call_price{0.0};
    double put_price{0.0};
    BatesGreeks greeks{};
    bool valid{false};
};

class BatesOptionPricer {
public:
    static constexpr size_t QUADRATURE_STEPS = 64;
    static constexpr double INTEGRATION_UPPER_BOUND = 80.0;

    static BatesPriceResult price_european_option(const BatesParameters& params) noexcept {
        BatesPriceResult result{};
        if (params.spot_price <= 0.0 || params.strike_price <= 0.0 || params.time_to_maturity <= 0.0) {
            return result;
        }

        double s0 = params.spot_price;
        double k = params.strike_price;
        double r = params.risk_free_rate;
        double q = params.dividend_yield;
        double t = params.time_to_maturity;

        double p1 = compute_prob(1, params);
        double p2 = compute_prob(2, params);

        double df_q = std::exp(-q * t);
        double df_r = std::exp(-r * t);

        double call = s0 * df_q * p1 - k * df_r * p2;
        call = std::max(0.0, std::max(call, s0 * df_q - k * df_r)); // Intrinsic floor

        // Put price by Put-Call Parity: P = C - S0 e^{-q T} + K e^{-r T}
        double put = call - s0 * df_q + k * df_r;
        put = std::max(0.0, put);

        result.call_price = call;
        result.put_price = put;
        result.greeks = compute_greeks(params, call);
        result.valid = true;
        return result;
    }

private:
    using Complex = std::complex<double>;

    static Complex bates_cf(int j, double phi, const BatesParameters& p) noexcept {
        Complex i(0.0, 1.0);
        double s0 = p.spot_price;
        double r = p.risk_free_rate;
        double q = p.dividend_yield;
        double t = p.time_to_maturity;
        double v0 = p.initial_variance;
        double kappa = p.mean_reversion_kappa;
        double theta = p.long_term_var_theta;
        double xi = p.vol_of_vol_xi;
        double rho = p.correlation_rho;
        double lambda = p.jump_intensity_lambda;
        double gamma = p.jump_mean_gamma;
        double delta = p.jump_vol_delta;

        double u = (j == 1) ? 0.5 : -0.5;
        double b = (j == 1) ? (kappa - rho * xi) : kappa;

        Complex d = std::sqrt(std::pow(b - rho * xi * phi * i, 2) - xi * xi * (2.0 * u * phi * i - phi * phi));
        Complex g = (b - rho * xi * phi * i - d) / (b - rho * xi * phi * i + d);

        Complex c_val = (r - q) * phi * i * t + (kappa * theta / (xi * xi)) *
            ((b - rho * xi * phi * i - d) * t - 2.0 * std::log((1.0 - g * std::exp(-d * t)) / (1.0 - g)));
        Complex d_val = ((b - rho * xi * phi * i - d) / (xi * xi)) *
            ((1.0 - std::exp(-d * t)) / (1.0 - g * std::exp(-d * t)));

        // Jump component:
        // k_bar = E[J - 1] = exp(gamma + 0.5 * delta^2) - 1
        double k_bar = std::exp(gamma + 0.5 * delta * delta) - 1.0;
        Complex psi_jump(0.0, 0.0);

        if (lambda > 0.0) {
            if (j == 2) {
                // Risk-neutral jump characteristic component
                Complex jump_term = std::exp(i * phi * gamma - 0.5 * phi * phi * delta * delta);
                psi_jump = lambda * t * (jump_term - 1.0 - i * phi * k_bar);
            } else {
                // Measure transformed jump component for P1 (stock measure)
                Complex jump_term = std::exp(i * phi * (gamma + delta * delta) - 0.5 * phi * phi * delta * delta + gamma + 0.5 * delta * delta);
                psi_jump = lambda * t * (jump_term - (1.0 + k_bar) - i * phi * k_bar);
            }
        }

        Complex f = std::exp(c_val + d_val * v0 + i * phi * std::log(s0) + psi_jump);
        return f;
    }

    static double compute_prob(int j, const BatesParameters& p) noexcept {
        Complex i(0.0, 1.0);
        double k = p.strike_price;
        double h = INTEGRATION_UPPER_BOUND / static_cast<double>(QUADRATURE_STEPS);
        double integral = 0.0;

        for (size_t step = 1; step <= QUADRATURE_STEPS; ++step) {
            double phi = (step - 0.5) * h;
            Complex cf = bates_cf(j, phi, p);
            Complex integrand = std::exp(-i * phi * std::log(k)) * cf / (i * phi);
            integral += integrand.real() * h;
        }

        double prob = 0.5 + (1.0 / M_PI) * integral;
        return std::clamp(prob, 0.0, 1.0);
    }

    static BatesGreeks compute_greeks(const BatesParameters& p, double base_call) noexcept {
        BatesGreeks g{};

        // Delta & Gamma via central finite difference
        double ds = p.spot_price * 0.01;
        BatesParameters p_up = p;
        p_up.spot_price += ds;
        double c_up = price_raw_call(p_up);

        BatesParameters p_down = p;
        p_down.spot_price -= ds;
        double c_down = price_raw_call(p_down);

        g.delta = (c_up - c_down) / (2.0 * ds);
        g.gamma = (c_up - 2.0 * base_call + c_down) / (ds * ds);

        // Vega (w.r.t initial volatility sigma0)
        double dvol = 0.01;
        double vol0 = std::sqrt(p.initial_variance);
        BatesParameters p_vol = p;
        p_vol.initial_variance = std::pow(vol0 + dvol, 2);
        double c_vega = price_raw_call(p_vol);
        g.vega = (c_vega - base_call) / dvol;

        // Theta
        double dt = 1.0 / 365.0;
        if (p.time_to_maturity > dt) {
            BatesParameters p_theta = p;
            p_theta.time_to_maturity -= dt;
            double c_theta = price_raw_call(p_theta);
            g.theta = (c_theta - base_call) / dt;
        }

        // Rho
        double dr = 0.001;
        BatesParameters p_rho = p;
        p_rho.risk_free_rate += dr;
        double c_rho = price_raw_call(p_rho);
        g.rho = (c_rho - base_call) / dr;

        // Jump intensity sensitivity
        double dlambda = 0.05;
        BatesParameters p_lam = p;
        p_lam.jump_intensity_lambda += dlambda;
        double c_lam = price_raw_call(p_lam);
        g.jump_lambda_sensitivity = (c_lam - base_call) / dlambda;

        return g;
    }

    static double price_raw_call(const BatesParameters& p) noexcept {
        double p1 = compute_prob(1, p);
        double p2 = compute_prob(2, p);
        double df_q = std::exp(-p.dividend_yield * p.time_to_maturity);
        double df_r = std::exp(-p.risk_free_rate * p.time_to_maturity);
        double call = p.spot_price * df_q * p1 - p.strike_price * df_r * p2;
        return std::max(0.0, call);
    }
};

} // namespace luv
