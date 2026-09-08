#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace routing {

struct VenueFeeSchedule {
    uint8_t venue_id = 0;
    char venue_name[8] = {0};
    double maker_rebate_bps = 0.0; // Rebate for adding liquidity (e.g. 20.0 = 0.20%)
    double taker_fee_bps = 0.0;    // Fee for removing liquidity (e.g. 30.0 = 0.30%)
    double routing_cost_bps = 1.0; // Fixed network connectivity cost
};

struct NetRoutedCost {
    uint8_t venue_id = 0;
    int64_t quoted_price = 0;
    int64_t effective_net_price = 0; // Price adjusted for fees/rebates
    double fee_cost_bps = 0.0;
};

class FeeRebateOptimizer {
public:
    static constexpr size_t kMaxVenues = 8;

    FeeRebateOptimizer() noexcept : num_venues_(0) {}

    void register_venue_fee(
        uint8_t venue_id,
        const char* name,
        double maker_rebate_bps,
        double taker_fee_bps,
        double routing_cost_bps = 1.0) noexcept
    {
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].venue_id == venue_id) {
                venues_[i].maker_rebate_bps = maker_rebate_bps;
                venues_[i].taker_fee_bps = taker_fee_bps;
                venues_[i].routing_cost_bps = routing_cost_bps;
                return;
            }
        }

        if (num_venues_ < kMaxVenues) {
            VenueFeeSchedule v;
            v.venue_id = venue_id;
            std::strncpy(v.venue_name, name ? name : "", sizeof(v.venue_name) - 1);
            v.maker_rebate_bps = maker_rebate_bps;
            v.taker_fee_bps = taker_fee_bps;
            v.routing_cost_bps = routing_cost_bps;
            venues_[num_venues_++] = v;
        }
    }

    // Computes effective net price for a taker aggressor order
    NetRoutedCost compute_taker_net_price(
        uint8_t venue_id,
        uint8_t side,
        int64_t quoted_price) const noexcept
    {
        NetRoutedCost res;
        res.venue_id = venue_id;
        res.quoted_price = quoted_price;

        const VenueFeeSchedule* sched = nullptr;
        for (size_t i = 0; i < num_venues_; ++i) {
            if (venues_[i].venue_id == venue_id) {
                sched = &venues_[i];
                break;
            }
        }

        if (!sched || quoted_price <= 0) {
            res.effective_net_price = quoted_price;
            return res;
        }

        // Net fee = Taker Fee + Routing Cost - Maker Rebate (if any)
        double net_fee_bps = sched->taker_fee_bps + sched->routing_cost_bps;
        res.fee_cost_bps = net_fee_bps;

        // Price adjustment in fixed-point cents
        int64_t fee_adjustment = static_cast<int64_t>(
            std::round(static_cast<double>(quoted_price) * (net_fee_bps / 10000.0)));

        if (side == exec::kBuy) {
            // Buyer pays quoted price + fee
            res.effective_net_price = quoted_price + fee_adjustment;
        } else {
            // Seller receives quoted price - fee
            res.effective_net_price = quoted_price - fee_adjustment;
        }

        return res;
    }

private:
    std::array<VenueFeeSchedule, kMaxVenues> venues_{};
    size_t num_venues_{0};
};

} // namespace routing
} // namespace luv
