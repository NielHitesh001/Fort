#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include "luv_yield_curve.hpp"

namespace luv {
namespace fixed_income {

enum class DayCountConvention : uint8_t {
    kAct360 = 0,
    kAct365 = 1,
    kThirty360 = 2
};

struct SwapLegDefinition {
    int64_t notional = 0;        // Scaled x10,000 (e.g. $10,000,000)
    double fixed_rate = 0.0;     // e.g. 0.045 = 4.5%
    double tenor_years = 5.0;    // 5-year swap
    double payment_frequency_years = 0.5; // Semi-annual
    DayCountConvention day_count = DayCountConvention::kAct360;
};

struct SwapValuationResult {
    double fixed_leg_pv = 0.0;
    double float_leg_pv = 0.0;
    double net_npv = 0.0;        // Float Leg PV - Fixed Leg PV (for Fixed Payer / Float Receiver)
    double par_swap_rate = 0.0;  // Fair fixed rate where NPV = 0
    double dv01 = 0.0;           // Dollar value of 1 basis point shift
};

class InterestRateSwapEngine {
public:
    InterestRateSwapEngine() noexcept = default;

    SwapValuationResult value_swap(const SwapLegDefinition& swap, const YieldCurveEngine& curve) const noexcept {
        SwapValuationResult result{};
        if (swap.notional <= 0 || swap.tenor_years <= 0.0 || swap.payment_frequency_years <= 0.0) {
            return result;
        }

        size_t num_payments = static_cast<size_t>(swap.tenor_years / swap.payment_frequency_years);
        double annuity = 0.0;
        double float_pv = 0.0;

        for (size_t i = 1; i <= num_payments; ++i) {
            double t_i = static_cast<double>(i) * swap.payment_frequency_years;
            double t_prev = t_i - swap.payment_frequency_years;

            double df_i = curve.get_discount_factor(t_i);
            annuity += swap.payment_frequency_years * df_i;

            // Forward rate between t_prev and t_i
            double fwd_rate = curve.get_forward_rate(t_prev, t_i);
            float_pv += static_cast<double>(swap.notional) * fwd_rate * swap.payment_frequency_years * df_i;
        }

        double fixed_pv = static_cast<double>(swap.notional) * swap.fixed_rate * annuity;

        result.fixed_leg_pv = fixed_pv;
        result.float_leg_pv = float_pv;
        result.net_npv = float_pv - fixed_pv;

        if (annuity > 0.0) {
            result.par_swap_rate = float_pv / (static_cast<double>(swap.notional) * annuity);
            // DV01 = Notional * Annuity * 0.0001 (1 bp shift)
            result.dv01 = static_cast<double>(swap.notional) * annuity * 0.0001 / 10000.0;
        }

        return result;
    }
};

} // namespace fixed_income
} // namespace luv
