#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace luv {

struct BasisCarryParams {
    double spot_price{50000.0};
    double perp_price{50100.0};
    double eight_hour_funding_rate{0.0003}; // 0.03% per 8h (+30 bps / day ~ 32.8% APR)
    double spot_borrow_annual_rate{0.05};  // 5% APR borrow cost (if shorting spot)
    double taker_fee_pct{0.0005};          // 0.05% taker fee
};

struct BasisCarryResult {
    double basis_spread_pct{0.0};     // (Perp - Spot) / Spot
    double annualized_funding_apr{0.0}; // 8h funding * 3 * 365
    double net_annualized_apy{0.0};   // Funding APR - Fees/Borrow
    bool is_carry_profitable{false};
};

class CryptoBasisArbitrageEngine {
public:
    static BasisCarryResult evaluate_carry(const BasisCarryParams& p) noexcept {
        BasisCarryResult res;
        if (p.spot_price <= 0.0) return res;

        // 1. Basis spread
        res.basis_spread_pct = (p.perp_price - p.spot_price) / p.spot_price;

        // 2. Annualized Funding APR (3 funding periods per 24 hours * 365 days)
        res.annualized_funding_apr = p.eight_hour_funding_rate * 3.0 * 365.0;

        // 3. Net APY for Cash-and-Carry (Long Spot + Short Perp)
        // Two round-trip taker fees (open spot, open perp, close spot, close perp) ~ 4 * taker_fee
        double entry_exit_fee_impact = 4.0 * p.taker_fee_pct;
        
        // Net APY assuming held for 1 year
        res.net_annualized_apy = res.annualized_funding_apr - entry_exit_fee_impact;

        // If negative funding (Short Spot + Long Perp), subtract borrow rate
        if (p.eight_hour_funding_rate < 0.0) {
            res.net_annualized_apy = std::abs(res.annualized_funding_apr) - entry_exit_fee_impact - p.spot_borrow_annual_rate;
        }

        res.is_carry_profitable = (res.net_annualized_apy > 0.05); // Minimum 5% net hurdle

        return res;
    }
};

} // namespace luv
