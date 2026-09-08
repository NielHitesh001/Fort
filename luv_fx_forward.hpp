#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace fx {

struct FxForwardPillar {
    double tenor_years = 0.0;    // e.g. 0.083 (1M), 0.25 (3M), 0.5 (6M), 1.0 (1Y)
    double swap_points_pips = 0.0; // e.g. +45.5 pips (1 pip = 0.0001 in standard FX)
};

class FxForwardCurveEngine {
public:
    static constexpr size_t kMaxPillars = 16;

    explicit FxForwardCurveEngine(double spot_rate = 1.0850) noexcept
        : spot_rate_(spot_rate), num_pillars_(0) {}

    void set_spot_rate(double spot) noexcept { spot_rate_ = spot; }

    bool add_forward_pillar(double tenor_years, double swap_points_pips) noexcept {
        if (tenor_years <= 0.0 || num_pillars_ >= kMaxPillars) return false;
        pillars_[num_pillars_++] = FxForwardPillar{tenor_years, swap_points_pips};

        std::sort(pillars_.begin(), pillars_.begin() + num_pillars_,
            [](const FxForwardPillar& a, const FxForwardPillar& b) {
                return a.tenor_years < b.tenor_years;
            });
        return true;
    }

    // Linearly interpolates swap points for broken date tenor
    double get_swap_points(double target_tenor_years) const noexcept {
        if (num_pillars_ == 0 || target_tenor_years <= 0.0) return 0.0;
        if (target_tenor_years <= pillars_[0].tenor_years) return pillars_[0].swap_points_pips;
        if (target_tenor_years >= pillars_[num_pillars_ - 1].tenor_years) return pillars_[num_pillars_ - 1].swap_points_pips;

        for (size_t i = 0; i < num_pillars_ - 1; ++i) {
            if (target_tenor_years >= pillars_[i].tenor_years && target_tenor_years <= pillars_[i + 1].tenor_years) {
                double t0 = pillars_[i].tenor_years;
                double t1 = pillars_[i + 1].tenor_years;
                double sp0 = pillars_[i].swap_points_pips;
                double sp1 = pillars_[i + 1].swap_points_pips;

                double weight = (target_tenor_years - t0) / (t1 - t0);
                return sp0 + weight * (sp1 - sp0);
            }
        }

        return pillars_[num_pillars_ - 1].swap_points_pips;
    }

    // Computes Forward Outright = Spot + (SwapPoints * 0.0001)
    double get_forward_outright(double target_tenor_years) const noexcept {
        double pips = get_swap_points(target_tenor_years);
        return spot_rate_ + (pips * 0.0001);
    }

private:
    double spot_rate_{1.0850};
    std::array<FxForwardPillar, kMaxPillars> pillars_{};
    size_t num_pillars_{0};
};

} // namespace fx
} // namespace luv
