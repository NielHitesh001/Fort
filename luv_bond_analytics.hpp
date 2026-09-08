#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace luv {
namespace fixed_income {

struct FixedCouponBond {
    double coupon_rate = 0.05;      // 5.0% annual coupon
    double face_value = 100.0;       // $100 par
    double maturity_years = 10.0;    // 10-year bond
    double payment_frequency = 0.5;  // Semi-annual
};

struct BondAnalyticsResult {
    double clean_price = 0.0;
    double macaulay_duration = 0.0;
    double modified_duration = 0.0;
    double convexity = 0.0;
    double dv01 = 0.0; // Dollar value of 1 bp shift per $100 face value
};

class BondAnalyticsEngine {
public:
    BondAnalyticsEngine() noexcept = default;

    // Computes Bond Price, Macaulay Duration, Modified Duration, and Convexity given YTM (annualized)
    BondAnalyticsResult compute_analytics(const FixedCouponBond& bond, double ytm) const noexcept {
        BondAnalyticsResult res{};
        if (bond.face_value <= 0.0 || bond.maturity_years <= 0.0 || bond.payment_frequency <= 0.0) return res;

        size_t n = static_cast<size_t>(bond.maturity_years / bond.payment_frequency);
        double c = bond.face_value * bond.coupon_rate * bond.payment_frequency;
        double y_per_period = ytm * bond.payment_frequency;

        double pv = 0.0;
        double weighted_time = 0.0;
        double convexity_sum = 0.0;

        for (size_t i = 1; i <= n; ++i) {
            double t = static_cast<double>(i) * bond.payment_frequency;
            double cash_flow = (i == n) ? (c + bond.face_value) : c;
            double df = std::pow(1.0 + y_per_period, -static_cast<double>(i));
            double pv_cf = cash_flow * df;

            pv += pv_cf;
            weighted_time += t * pv_cf;
            convexity_sum += static_cast<double>(i) * (static_cast<double>(i) + 1.0) * pv_cf;
        }

        res.clean_price = pv;

        if (pv > 0.0) {
            res.macaulay_duration = weighted_time / pv;
            res.modified_duration = res.macaulay_duration / (1.0 + y_per_period);
            
            double period_convexity = convexity_sum / (pv * std::pow(1.0 + y_per_period, 2.0));
            res.convexity = period_convexity * std::pow(bond.payment_frequency, 2.0);

            // DV01 = Price * ModDuration * 0.0001
            res.dv01 = pv * res.modified_duration * 0.0001;
        }

        return res;
    }

    // Newton-Raphson solver to compute YTM from target market price
    double solve_ytm_from_price(const FixedCouponBond& bond, double target_price, double initial_guess = 0.05) const noexcept {
        double ytm = initial_guess;
        for (int iter = 0; iter < 100; ++iter) {
            auto analytics = compute_analytics(bond, ytm);
            double diff = analytics.clean_price - target_price;
            if (std::fabs(diff) < 1e-7) break;

            double derivative = -analytics.clean_price * analytics.modified_duration;
            if (std::fabs(derivative) < 1e-12) break;

            ytm = ytm - diff / derivative;
            if (ytm <= 0.0) ytm = 0.0001;
        }
        return ytm;
    }
};

} // namespace fixed_income
} // namespace luv
