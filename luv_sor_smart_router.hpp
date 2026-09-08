#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

struct VenueQuote {
    uint32_t venue_id{0};
    bool is_dark{false};
    uint64_t bid_price{0};
    uint64_t bid_size{0};
    uint64_t ask_price{0};
    uint64_t ask_size{0};
    int32_t fee_or_rebate_bps{0}; // Negative = fee, Positive = maker rebate
    uint32_t fill_probability_pct{100}; // Historical fill rate (0-100%)
};

struct RoutingSlice {
    uint32_t venue_id{0};
    uint64_t route_quantity{0};
    uint64_t target_price{0};
    bool is_dark{false};
};

struct RoutingPlan {
    std::array<RoutingSlice, 8> slices{};
    size_t slice_count{0};
    uint64_t total_routed_qty{0};
    uint64_t remaining_unfilled_qty{0};
};

class SmartOrderRouter {
public:
    static constexpr size_t kMaxVenues = 8;

    // Routes an incoming aggressive order across Dark Midpoint & Lit Venues optimizing for Price, Fee/Rebate, and Fill Rate
    static RoutingPlan route_order(
        bool is_buy,
        uint64_t order_price,
        uint64_t order_quantity,
        const VenueQuote* venues,
        size_t venue_count) noexcept
    {
        RoutingPlan plan;
        if (order_quantity == 0 || venue_count == 0) return plan;

        uint64_t rem_qty = order_quantity;

        // Step 1: Dark Midpoint Sweeping (if available and matching limit price)
        for (size_t i = 0; i < venue_count && rem_qty > 0; ++i) {
            const auto& v = venues[i];
            if (!v.is_dark) continue;

            uint64_t dark_price = is_buy ? v.ask_price : v.bid_price;
            uint64_t dark_avail = is_buy ? v.ask_size : v.bid_size;

            if (dark_avail > 0 && dark_price > 0) {
                bool price_acceptable = is_buy ? (dark_price <= order_price) : (dark_price >= order_price);
                if (price_acceptable) {
                    uint64_t fill_qty = std::min(rem_qty, dark_avail);
                    auto& s = plan.slices[plan.slice_count++];
                    s.venue_id = v.venue_id;
                    s.is_dark = true;
                    s.target_price = dark_price;
                    s.route_quantity = fill_qty;

                    rem_qty -= fill_qty;
                    plan.total_routed_qty += fill_qty;
                }
            }
        }

        // Step 2: Lit Venues Sweeping sorted by best price, then best fee/rebate, then fill probability
        // Rank lit venues
        size_t lit_indices[kMaxVenues];
        size_t lit_count = 0;
        for (size_t i = 0; i < venue_count; ++i) {
            if (!venues[i].is_dark) {
                lit_indices[lit_count++] = i;
            }
        }

        // Insertion sort lit venues by best execution quality
        for (size_t i = 1; i < lit_count; ++i) {
            size_t key = lit_indices[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0) {
                const auto& va = venues[lit_indices[j]];
                const auto& vb = venues[key];

                uint64_t pa = is_buy ? va.ask_price : va.bid_price;
                uint64_t pb = is_buy ? vb.ask_price : vb.bid_price;

                bool b_better = false;
                if (is_buy) {
                    if (pb < pa) b_better = true;
                    else if (pb == pa && vb.fee_or_rebate_bps > va.fee_or_rebate_bps) b_better = true;
                    else if (pb == pa && vb.fee_or_rebate_bps == va.fee_or_rebate_bps && vb.fill_probability_pct > va.fill_probability_pct) b_better = true;
                } else {
                    if (pb > pa) b_better = true;
                    else if (pb == pa && vb.fee_or_rebate_bps > va.fee_or_rebate_bps) b_better = true;
                    else if (pb == pa && vb.fee_or_rebate_bps == va.fee_or_rebate_bps && vb.fill_probability_pct > va.fill_probability_pct) b_better = true;
                }

                if (b_better) {
                    lit_indices[j + 1] = lit_indices[j];
                    --j;
                } else {
                    break;
                }
            }
            lit_indices[j + 1] = key;
        }

        // Sweep lit venues according to ranking
        for (size_t i = 0; i < lit_count && rem_qty > 0; ++i) {
            const auto& v = venues[lit_indices[i]];
            uint64_t price = is_buy ? v.ask_price : v.bid_price;
            uint64_t avail = is_buy ? v.ask_size : v.bid_size;

            if (avail > 0 && price > 0) {
                bool price_acceptable = is_buy ? (price <= order_price) : (price >= order_price);
                if (price_acceptable) {
                    uint64_t fill_qty = std::min(rem_qty, avail);
                    auto& s = plan.slices[plan.slice_count++];
                    s.venue_id = v.venue_id;
                    s.is_dark = false;
                    s.target_price = price;
                    s.route_quantity = fill_qty;

                    rem_qty -= fill_qty;
                    plan.total_routed_qty += fill_qty;
                }
            }
        }

        plan.remaining_unfilled_qty = rem_qty;
        return plan;
    }
};

} // namespace luv
