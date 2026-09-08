#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace algo {

// TWAP (Time-Weighted Average Price) Slicer
class TwapSlicer {
public:
    static int64_t compute_next_slice(
        int64_t total_qty,
        int64_t cum_filled_qty,
        uint32_t current_interval_idx,
        uint32_t total_intervals) noexcept
    {
        if (current_interval_idx >= total_intervals || cum_filled_qty >= total_qty) {
            return 0;
        }

        const uint32_t intervals_remaining = total_intervals - current_interval_idx;
        const int64_t qty_remaining = total_qty - cum_filled_qty;

        int64_t slice = qty_remaining / intervals_remaining;
        if (slice == 0 && qty_remaining > 0) slice = 1;
        return std::min(slice, qty_remaining);
    }
};

// VWAP (Volume-Weighted Average Price) Curve Slicer
class VwapSlicer {
public:
    // Historical intraday volume profile across 13 half-hour buckets (09:30 -> 16:00)
    // U-shaped: higher morning open and afternoon close
    static constexpr std::array<double, 13> kDefaultVolumeProfile = {
        0.14, // 09:30 - 10:00 (Open surge)
        0.10, // 10:00 - 10:30
        0.08, // 10:30 - 11:00
        0.07, // 11:00 - 11:30
        0.06, // 11:30 - 12:00
        0.05, // 12:00 - 12:30 (Lunch lull)
        0.05, // 12:30 - 13:00
        0.06, // 13:00 - 13:30
        0.07, // 13:30 - 14:00
        0.08, // 14:00 - 14:30
        0.09, // 14:30 - 15:00
        0.12, // 15:00 - 15:30
        0.16  // 15:30 - 16:00 (MOC / Close surge)
    };

    static int64_t compute_bucket_target(
        int64_t total_parent_qty,
        uint32_t bucket_idx) noexcept
    {
        if (bucket_idx >= kDefaultVolumeProfile.size()) return 0;
        const double weight = kDefaultVolumeProfile[bucket_idx];
        return static_cast<int64_t>(total_parent_qty * weight);
    }
};

// Participation Rate (POV - Percentage of Volume)
class PovSlicer {
public:
    static int64_t compute_child_qty(
        int64_t remaining_order_qty,
        int64_t interval_market_volume,
        double target_participation_pct) noexcept
    {
        if (remaining_order_qty <= 0 || interval_market_volume <= 0 || target_participation_pct <= 0.0) {
            return 0;
        }

        const int64_t target_qty = static_cast<int64_t>(interval_market_volume * (target_participation_pct / 100.0));
        return std::min(remaining_order_qty, std::max<int64_t>(1, target_qty));
    }
};

} // namespace algo
} // namespace luv
