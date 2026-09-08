#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace fixed_income {

struct YieldPillar {
    double tenor_years = 0.0; // e.g. 0.25 (3M), 0.5 (6M), 1.0 (1Y), 2.0 (2Y), 5.0 (5Y), 10.0 (10Y)
    double zero_rate = 0.0;   // Continuous zero rate (e.g. 0.045 = 4.50%)
};

class YieldCurveEngine {
public:
    static constexpr size_t kMaxPillars = 16;

    YieldCurveEngine() noexcept : num_pillars_(0) {}

    bool add_pillar(double tenor_years, double zero_rate) noexcept {
        if (tenor_years <= 0.0 || num_pillars_ >= kMaxPillars) return false;

        pillars_[num_pillars_++] = YieldPillar{tenor_years, zero_rate};

        // Keep sorted by tenor
        std::sort(pillars_.begin(), pillars_.begin() + num_pillars_,
            [](const YieldPillar& a, const YieldPillar& b) {
                return a.tenor_years < b.tenor_years;
            });
        return true;
    }

    // Linearly interpolates zero rate for any target tenor t
    double get_zero_rate(double target_tenor_years) const noexcept {
        if (num_pillars_ == 0 || target_tenor_years <= 0.0) return 0.0;

        if (target_tenor_years <= pillars_[0].tenor_years) {
            return pillars_[0].zero_rate;
        }

        if (target_tenor_years >= pillars_[num_pillars_ - 1].tenor_years) {
            return pillars_[num_pillars_ - 1].zero_rate;
        }

        // Find surrounding interval
        for (size_t i = 0; i < num_pillars_ - 1; ++i) {
            if (target_tenor_years >= pillars_[i].tenor_years && target_tenor_years <= pillars_[i + 1].tenor_years) {
                double t0 = pillars_[i].tenor_years;
                double t1 = pillars_[i + 1].tenor_years;
                double r0 = pillars_[i].zero_rate;
                double r1 = pillars_[i + 1].zero_rate;

                double weight = (target_tenor_years - t0) / (t1 - t0);
                return r0 + weight * (r1 - r0);
            }
        }

        return pillars_[num_pillars_ - 1].zero_rate;
    }

    // Computes Discount Factor: DF(t) = exp(-r(t) * t)
    double get_discount_factor(double target_tenor_years) const noexcept {
        if (target_tenor_years <= 0.0) return 1.0;
        double rate = get_zero_rate(target_tenor_years);
        return std::exp(-rate * target_tenor_years);
    }

    // Computes Forward Rate between t1 and t2: f(t1, t2) = (r2*t2 - r1*t1) / (t2 - t1)
    double get_forward_rate(double t1, double t2) const noexcept {
        if (t2 <= t1) return 0.0;
        double r1 = get_zero_rate(t1);
        double r2 = get_zero_rate(t2);
        return (r2 * t2 - r1 * t1) / (t2 - t1);
    }

private:
    std::array<YieldPillar, kMaxPillars> pillars_{};
    size_t num_pillars_{0};
};

} // namespace fixed_income
} // namespace luv
