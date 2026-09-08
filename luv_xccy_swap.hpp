#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace fixed_income {

struct CrossCurrencyQuote {
    uint16_t base_ccy_id = 0;   // e.g. 1 (EUR)
    uint16_t quote_ccy_id = 0;  // e.g. 2 (USD)
    double spot_fx_rate = 1.0850; // EUR/USD
    double base_zero_rate = 0.0350; // EUR 3.50%
    double quote_zero_rate = 0.0500; // USD 5.00%
    double basis_spread_bps = -25.0; // Basis spread on EUR leg (-25 bps)
    double tenor_years = 1.0;
};

struct XccyValuationResult {
    double theoretical_forward_fx = 0.0;
    double basis_adjusted_forward_fx = 0.0;
    double cip_deviation_bps = 0.0; // Covered Interest Parity Disparity
    bool arbitrage_opportunity = false;
};

class CrossCurrencySwapEngine {
public:
    CrossCurrencySwapEngine() noexcept = default;

    // Evaluates Cross-Currency Swap & Covered Interest Parity (CIP) Deviations
    XccyValuationResult evaluate_xccy_swap(const CrossCurrencyQuote& q) const noexcept {
        XccyValuationResult res{};
        if (q.spot_fx_rate <= 0.0 || q.tenor_years <= 0.0) return res;

        // 1. Classical Covered Interest Parity Forward: F = S * exp((r_quote - r_base) * T)
        double r_base = q.base_zero_rate;
        double r_quote = q.quote_zero_rate;
        double T = q.tenor_years;

        res.theoretical_forward_fx = q.spot_fx_rate * std::exp((r_quote - r_base) * T);

        // 2. Basis-adjusted Forward: F_basis = S * exp((r_quote - (r_base + basis)) * T)
        double basis_rate = q.basis_spread_bps / 10000.0;
        res.basis_adjusted_forward_fx = q.spot_fx_rate * std::exp((r_quote - (r_base + basis_rate)) * T);

        // 3. CIP Deviation in basis points
        double diff = res.basis_adjusted_forward_fx - res.theoretical_forward_fx;
        res.cip_deviation_bps = (diff / res.theoretical_forward_fx) * 10000.0;

        // Arbitrage opportunity if basis disparity > 15 bps
        res.arbitrage_opportunity = (std::fabs(res.cip_deviation_bps) >= 15.0);

        return res;
    }
};

} // namespace fixed_income
} // namespace luv
