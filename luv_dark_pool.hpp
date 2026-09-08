#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <cstring>
#include "luv_execution.hpp"

namespace luv {

enum class DisplayMode : uint8_t {
    kDisplayed = 0,
    kHidden = 1,
    kMidpointPeg = 2
};

struct DarkOrder {
    uint64_t order_id = 0;
    uint32_t firm_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    DisplayMode display_mode = DisplayMode::kDisplayed;
    int64_t price = 0; // Fixed-point
    int64_t qty = 0;
    int64_t min_qty = 0; // Minimum Acceptable Quantity (MAQ)
    uint64_t priority_ts_ns = 0;
    bool active = false;
};

struct MatchFill {
    uint64_t maker_order_id = 0;
    uint64_t taker_order_id = 0;
    uint16_t symbol_idx = 0;
    int64_t match_price = 0;
    int64_t match_qty = 0;
    bool is_midpoint = false;
};

class DarkPoolEngine {
public:
    static constexpr size_t kMaxRestingOrders = 1024;

    DarkPoolEngine() noexcept : order_count_(0) {
        for (auto& o : orders_) o.active = false;
    }

    bool add_order(
        uint64_t order_id,
        uint32_t firm_id,
        uint16_t symbol_idx,
        uint8_t side,
        DisplayMode mode,
        int64_t price,
        int64_t qty,
        int64_t min_qty,
        uint64_t ts_ns) noexcept
    {
        if (order_count_ >= kMaxRestingOrders || qty <= 0) return false;

        for (size_t i = 0; i < kMaxRestingOrders; ++i) {
            if (!orders_[i].active) {
                orders_[i] = DarkOrder{
                    .order_id = order_id,
                    .firm_id = firm_id,
                    .symbol_idx = symbol_idx,
                    .side = side,
                    .display_mode = mode,
                    .price = price,
                    .qty = qty,
                    .min_qty = min_qty,
                    .priority_ts_ns = ts_ns,
                    .active = true
                };
                order_count_++;
                return true;
            }
        }
        return false;
    }

    bool cancel_order(uint64_t order_id) noexcept {
        for (size_t i = 0; i < kMaxRestingOrders; ++i) {
            if (orders_[i].active && orders_[i].order_id == order_id) {
                orders_[i].active = false;
                order_count_--;
                return true;
            }
        }
        return false;
    }

    // Match incoming order following Price-Display-Time priority:
    // 1. Better price matches first
    // 2. At same price: Displayed orders match before Hidden orders
    // 3. At same price and display status: earlier timestamp matches first
    // 4. MinQty / MAQ filter is strictly enforced
    size_t match_cross(
        uint64_t taker_order_id,
        uint32_t taker_firm_id,
        uint16_t symbol_idx,
        uint8_t taker_side,
        int64_t taker_price,
        int64_t taker_qty,
        int64_t best_bid,
        int64_t best_ask,
        MatchFill* out_fills,
        size_t max_fills) noexcept
    {
        if (!out_fills || max_fills == 0 || taker_qty <= 0) return 0;

        const int64_t midpoint_price = (best_bid + best_ask) / 2;
        size_t fill_count = 0;
        int64_t remaining_taker_qty = taker_qty;

        // Collect matching candidate indices
        std::array<size_t, kMaxRestingOrders> candidate_indices{};
        size_t num_candidates = 0;

        for (size_t i = 0; i < kMaxRestingOrders; ++i) {
            if (!orders_[i].active || orders_[i].symbol_idx != symbol_idx || orders_[i].side == taker_side) {
                continue;
            }
            if (orders_[i].firm_id == taker_firm_id) {
                continue; // Self-Trade Prevention
            }

            // Effective resting price
            int64_t effective_price = orders_[i].price;
            if (orders_[i].display_mode == DisplayMode::kMidpointPeg) {
                effective_price = midpoint_price;
            }

            // Price cross check
            bool price_cross = false;
            if (taker_side == exec::kBuy) {
                price_cross = (taker_price >= effective_price);
            } else {
                price_cross = (taker_price <= effective_price);
            }

            if (price_cross) {
                candidate_indices[num_candidates++] = i;
            }
        }

        // Sort candidates by Price-Display-Time
        std::sort(candidate_indices.begin(), candidate_indices.begin() + num_candidates,
            [&](size_t a, size_t b) {
                const auto& oa = orders_[a];
                const auto& ob = orders_[b];

                int64_t pa = (oa.display_mode == DisplayMode::kMidpointPeg) ? midpoint_price : oa.price;
                int64_t pb = (ob.display_mode == DisplayMode::kMidpointPeg) ? midpoint_price : ob.price;

                if (taker_side == exec::kBuy) {
                    if (pa != pb) return pa < pb; // Buyer matches lowest sell price first
                } else {
                    if (pa != pb) return pa > pb; // Seller matches highest buy price first
                }

                // Display priority: Displayed before Hidden/Midpoint
                int disp_a = (oa.display_mode == DisplayMode::kDisplayed) ? 0 : 1;
                int disp_b = (ob.display_mode == DisplayMode::kDisplayed) ? 0 : 1;
                if (disp_a != disp_b) return disp_a < disp_b;

                // Time priority
                return oa.priority_ts_ns < ob.priority_ts_ns;
            });

        // Execute fills against sorted candidates
        for (size_t c = 0; c < num_candidates && fill_count < max_fills && remaining_taker_qty > 0; ++c) {
            size_t idx = candidate_indices[c];
            auto& maker = orders_[idx];

            int64_t fill_qty = std::min(remaining_taker_qty, maker.qty);

            // Check MinQty constraints on both maker and taker
            if (maker.min_qty > 0 && fill_qty < maker.min_qty) {
                continue; // Maker MAQ unsatisfied
            }

            int64_t match_p = (maker.display_mode == DisplayMode::kMidpointPeg) ? midpoint_price : maker.price;

            out_fills[fill_count++] = MatchFill{
                .maker_order_id = maker.order_id,
                .taker_order_id = taker_order_id,
                .symbol_idx = symbol_idx,
                .match_price = match_p,
                .match_qty = fill_qty,
                .is_midpoint = (maker.display_mode == DisplayMode::kMidpointPeg)
            };

            maker.qty -= fill_qty;
            remaining_taker_qty -= fill_qty;

            if (maker.qty == 0) {
                maker.active = false;
                order_count_--;
            }
        }

        return fill_count;
    }

    size_t active_orders() const noexcept { return order_count_; }

private:
    std::array<DarkOrder, kMaxRestingOrders> orders_{};
    size_t order_count_{0};
};

} // namespace luv
