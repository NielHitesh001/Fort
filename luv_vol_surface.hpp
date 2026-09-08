#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace derivatives {

struct VolGridPoint {
    double expiry_years = 0.0;
    int64_t strike_price = 0; // Scaled price (x10,000)
    double implied_vol = 0.0; // e.g. 0.25 = 25%
};

class VolatilitySurfaceEngine {
public:
    static constexpr size_t kMaxGridPoints = 64;

    VolatilitySurfaceEngine() noexcept : num_points_(0) {}

    bool add_vol_point(double expiry_years, int64_t strike_price, double implied_vol) noexcept {
        if (expiry_years <= 0.0 || strike_price <= 0 || implied_vol <= 0.0 || num_points_ >= kMaxGridPoints) {
            return false;
        }

        grid_[num_points_++] = VolGridPoint{expiry_years, strike_price, implied_vol};
        return true;
    }

    // Bilinear or inverse distance interpolation of implied volatility
    double get_implied_vol(double target_expiry_years, int64_t target_strike) const noexcept {
        if (num_points_ == 0) return 0.20; // 20% default baseline
        if (num_points_ == 1) return grid_[0].implied_vol;

        double sum_weights = 0.0;
        double weighted_vol = 0.0;

        for (size_t i = 0; i < num_points_; ++i) {
            const auto& pt = grid_[i];
            double dt = pt.expiry_years - target_expiry_years;
            double dk = static_cast<double>(pt.strike_price - target_strike) / 10000.0;

            double dist_sq = dt * dt + (dk * dk * 0.01);
            if (dist_sq < 1e-8) {
                return pt.implied_vol; // Exact match
            }

            double w = 1.0 / dist_sq;
            weighted_vol += w * pt.implied_vol;
            sum_weights += w;
        }

        if (sum_weights <= 0.0) return grid_[0].implied_vol;
        return weighted_vol / sum_weights;
    }

    size_t point_count() const noexcept { return num_points_; }

private:
    std::array<VolGridPoint, kMaxGridPoints> grid_{};
    size_t num_points_{0};
};

} // namespace derivatives
} // namespace luv
