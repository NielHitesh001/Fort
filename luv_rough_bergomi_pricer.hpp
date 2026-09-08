#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <array>

namespace luv {

struct RoughBergomiParameters {
    double spot_price{100.0};       // S0
    double strike_price{100.0};     // K
    double risk_free_rate{0.03};    // r
    double time_to_maturity{0.25};  // T (years, short-dated)
    double initial_forward_var{0.04}; // xi0(t) (sigma0 = 20%)
    double hurst_parameter{0.10};   // H in (0, 0.5), typically 0.07 - 0.15
    double vol_of_vol_eta{1.50};    // eta
    double correlation_rho{-0.75};  // rho (leverage effect)
};

struct RoughBergomiResult {
    double call_price{0.0};
    double put_price{0.0};
    double atm_skew{0.0};           // d(sigma_BS)/d(ln K) at ATM
    double effective_volatility{0.0};
    bool valid{false};
};

class RoughBergomiPricer {
public:
    static constexpr size_t kGridSteps = 32;

    static RoughBergomiResult price_option(const RoughBergomiParameters& p) noexcept {
        RoughBergomiResult res{};
        if (p.spot_price <= 0.0 || p.strike_price <= 0.0 || p.time_to_maturity <= 0.0 || 
            p.hurst_parameter <= 0.0 || p.hurst_parameter >= 0.5) 
        {
            return res;
        }

        double s0 = p.spot_price;
        double k = p.strike_price;
        double r = p.risk_free_rate;
        double t = p.time_to_maturity;
        double h = p.hurst_parameter;
        double eta = p.vol_of_vol_eta;
        double rho = p.correlation_rho;
        double xi0 = p.initial_forward_var;

        // 1. Compute theoretical ATM Skew according to Bayer-Friz-Gatheral (2016):
        // Skew_ATM ~ [rho * eta / (2 * (H + 0.5))] * T^{H - 0.5}
        double skew_factor = (rho * eta) / (2.0 * (h + 0.5));
        res.atm_skew = skew_factor * std::pow(t, h - 0.5);

        // 2. Compute effective integrated variance with rough correction
        // E[v_t] = xi0 * exp(0) = xi0
        // Var(v_t) scales with t^{2H}
        double var_drift_adj = 0.5 * eta * eta * std::pow(t, 2.0 * h);
        double total_var = xi0 * t * (1.0 + 0.5 * rho * eta * std::pow(t, h));
        total_var = std::max(1e-6, total_var);
        double total_vol = std::sqrt(total_var);

        // 3. Black-Scholes formula with rough skew shift
        double f = s0 * std::exp(r * t);
        double log_moneyness = std::log(s0 / k) + r * t;
        
        // Adjust sigma by moneyness skew: sigma(k) ~ sigma_0 + skew * ln(k / f)
        double sigma_k = std::sqrt(xi0) + res.atm_skew * std::log(k / f);
        sigma_k = std::max(0.01, std::min(2.5, sigma_k));

        double d1 = (std::log(s0 / k) + (r + 0.5 * sigma_k * sigma_k) * t) / (sigma_k * std::sqrt(t));
        double d2 = d1 - sigma_k * std::sqrt(t);

        double nd1 = 0.5 * std::erfc(-d1 / std::sqrt(2.0));
        double nd2 = 0.5 * std::erfc(-d2 / std::sqrt(2.0));

        double df = std::exp(-r * t);
        res.call_price = s0 * nd1 - k * df * nd2;
        res.call_price = std::max(0.0, std::max(res.call_price, s0 - k * df));

        // Put-Call Parity: P = C - S0 + K e^{-r T}
        res.put_price = res.call_price - s0 + k * df;
        res.put_price = std::max(0.0, res.put_price);

        res.effective_volatility = sigma_k;
        res.valid = true;
        return res;
    }
};

} // namespace luv
