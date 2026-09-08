#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>
#include "luv_yield_curve.hpp"

namespace luv {
namespace fixed_income {

struct CdsContractDefinition {
    int64_t notional = 0;             // Scaled x10,000 (e.g. $10,000,000)
    double running_spread_bps = 100.0; // Standard 100 bps (1.00%) or 500 bps (5.00%)
    double market_spread_bps = 120.0;  // Quoted par market spread
    double recovery_rate = 0.40;       // Standard ISDA 40% recovery for Senior Unsecured
    double tenor_years = 5.0;          // 5-Year CDS
    double payment_frequency = 0.25;   // Quarterly (ISDA standard)
};

struct CdsValuationResult {
    double hazard_rate = 0.0;          // Constant hazard rate lambda = Spread / (1 - R)
    double survival_probability_5y = 0.0;
    double default_leg_pv = 0.0;
    double premium_leg_pv = 0.0;
    double upfront_payment = 0.0;      // Upfront points / dollar amount to buy protection
    double cs01 = 0.0;                 // Credit spread 1 bp sensitivity
};

class CreditDefaultSwapEngine {
public:
    CreditDefaultSwapEngine() noexcept = default;

    CdsValuationResult value_cds(const CdsContractDefinition& cds, const YieldCurveEngine& discount_curve) const noexcept {
        CdsValuationResult res{};
        if (cds.notional <= 0 || cds.tenor_years <= 0.0 || cds.recovery_rate >= 1.0) return res;

        // 1. Implied constant hazard rate: lambda = MarketSpread / (1 - RecoveryRate)
        double market_spread = (cds.market_spread_bps / 10000.0);
        double loss_given_default = (1.0 - cds.recovery_rate);
        double lambda = market_spread / loss_given_default;
        res.hazard_rate = lambda;

        // 5-year survival probability: Q(T) = exp(-lambda * T)
        res.survival_probability_5y = std::exp(-lambda * cds.tenor_years);

        size_t num_periods = static_cast<size_t>(cds.tenor_years / cds.payment_frequency);
        double premium_pv = 0.0;
        double default_pv = 0.0;
        double running_coupon = cds.running_spread_bps / 10000.0;

        for (size_t i = 1; i <= num_periods; ++i) {
            double t_i = static_cast<double>(i) * cds.payment_frequency;
            double t_prev = t_i - cds.payment_frequency;

            double df_i = discount_curve.get_discount_factor(t_i);
            double df_mid = discount_curve.get_discount_factor((t_prev + t_i) * 0.5);

            double surv_i = std::exp(-lambda * t_i);
            double surv_prev = std::exp(-lambda * t_prev);

            // Premium payment on surviving notional
            premium_pv += static_cast<double>(cds.notional) * running_coupon * cds.payment_frequency * surv_i * df_i;

            // Default probability in interval (t_prev, t_i)
            double default_prob = surv_prev - surv_i;
            default_pv += static_cast<double>(cds.notional) * loss_given_default * default_prob * df_mid;
        }

        res.premium_leg_pv = premium_pv;
        res.default_leg_pv = default_pv;
        // Upfront = Default Leg PV - Premium Leg PV
        res.upfront_payment = default_pv - premium_pv;

        // CS01: 1 basis point shift in credit spread
        res.cs01 = static_cast<double>(cds.notional) * (cds.tenor_years * 0.0001) / 10000.0;

        return res;
    }
};

} // namespace fixed_income
} // namespace luv
