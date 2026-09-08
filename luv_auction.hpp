#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace auction {

enum class AuctionPhase : uint8_t {
    kContinuous = 0,
    kPreOpen = 1,
    kOpeningAuction = 2,
    kClosingAuction = 3,
    kVolatilityHalt = 4
};

enum class ImbalanceSide : uint8_t {
    kNone = 0,
    kBuyImbalance = 1,
    kSellImbalance = 2
};

struct AuctionOrder {
    uint64_t order_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    int64_t price = 0; // Fixed point price (0 = Market on Open/Close)
    int64_t qty = 0;
    bool is_market = false;
    bool active = false;
};

// Net Order Imbalance Indicator (NOII) summary
struct NoiiSnapshot {
    uint16_t symbol_idx = 0;
    int64_t indicative_match_price = 0;
    int64_t paired_qty = 0;
    int64_t imbalance_qty = 0;
    ImbalanceSide imbalance_side = ImbalanceSide::kNone;
    int64_t far_price = 0;
    int64_t near_price = 0;
};

class AuctionCrossEngine {
public:
    static constexpr size_t kMaxAuctionOrders = 2048;

    AuctionCrossEngine() noexcept : order_count_(0) {
        for (auto& o : orders_) o.active = false;
    }

    bool add_auction_order(
        uint64_t order_id,
        uint16_t symbol_idx,
        uint8_t side,
        int64_t price,
        int64_t qty,
        bool is_market) noexcept
    {
        if (order_count_ >= kMaxAuctionOrders || qty <= 0) return false;

        for (size_t i = 0; i < kMaxAuctionOrders; ++i) {
            if (!orders_[i].active) {
                orders_[i] = AuctionOrder{
                    .order_id = order_id,
                    .symbol_idx = symbol_idx,
                    .side = side,
                    .price = is_market ? 0 : price,
                    .qty = qty,
                    .is_market = is_market,
                    .active = true
                };
                order_count_++;
                return true;
            }
        }
        return false;
    }

    // Computes Indicative Match Price (IMP) that maximizes matched paired volume
    NoiiSnapshot compute_noii(uint16_t symbol_idx, int64_t reference_price) const noexcept {
        NoiiSnapshot noii;
        noii.symbol_idx = symbol_idx;

        // Collect unique price levels from active limit orders
        std::array<int64_t, kMaxAuctionOrders> price_levels{};
        size_t num_prices = 0;

        for (size_t i = 0; i < kMaxAuctionOrders; ++i) {
            if (orders_[i].active && orders_[i].symbol_idx == symbol_idx && !orders_[i].is_market) {
                int64_t p = orders_[i].price;
                bool exists = false;
                for (size_t j = 0; j < num_prices; ++j) {
                    if (price_levels[j] == p) { exists = true; break; }
                }
                if (!exists && num_prices < price_levels.size()) {
                    price_levels[num_prices++] = p;
                }
            }
        }

        if (num_prices == 0) {
            // Only market orders or no orders
            noii.indicative_match_price = reference_price;
            return noii;
        }

        std::sort(price_levels.begin(), price_levels.begin() + num_prices);

        int64_t best_price = reference_price;
        int64_t max_paired = 0;
        int64_t min_imbalance = INT64_MAX;

        for (size_t p_idx = 0; p_idx < num_prices; ++p_idx) {
            int64_t test_p = price_levels[p_idx];

            int64_t buy_vol = 0;
            int64_t sell_vol = 0;

            for (size_t i = 0; i < kMaxAuctionOrders; ++i) {
                if (!orders_[i].active || orders_[i].symbol_idx != symbol_idx) continue;

                if (orders_[i].side == exec::kBuy) {
                    if (orders_[i].is_market || orders_[i].price >= test_p) {
                        buy_vol += orders_[i].qty;
                    }
                } else {
                    if (orders_[i].is_market || orders_[i].price <= test_p) {
                        sell_vol += orders_[i].qty;
                    }
                }
            }

            int64_t paired = std::min(buy_vol, sell_vol);
            int64_t imbalance = std::abs(buy_vol - sell_vol);

            if (paired > max_paired || (paired == max_paired && imbalance < min_imbalance)) {
                max_paired = paired;
                min_imbalance = imbalance;
                best_price = test_p;
            }
        }

        // Calculate final buy/sell volume at best_price
        int64_t total_buy = 0;
        int64_t total_sell = 0;
        for (size_t i = 0; i < kMaxAuctionOrders; ++i) {
            if (!orders_[i].active || orders_[i].symbol_idx != symbol_idx) continue;
            if (orders_[i].side == exec::kBuy) {
                if (orders_[i].is_market || orders_[i].price >= best_price) total_buy += orders_[i].qty;
            } else {
                if (orders_[i].is_market || orders_[i].price <= best_price) total_sell += orders_[i].qty;
            }
        }

        noii.indicative_match_price = best_price;
        noii.paired_qty = std::min(total_buy, total_sell);
        noii.imbalance_qty = std::abs(total_buy - total_sell);
        if (total_buy > total_sell) {
            noii.imbalance_side = ImbalanceSide::kBuyImbalance;
        } else if (total_sell > total_buy) {
            noii.imbalance_side = ImbalanceSide::kSellImbalance;
        } else {
            noii.imbalance_side = ImbalanceSide::kNone;
        }

        return noii;
    }

    void reset() noexcept {
        for (auto& o : orders_) o.active = false;
        order_count_ = 0;
    }

    size_t order_count() const noexcept { return order_count_; }

private:
    std::array<AuctionOrder, kMaxAuctionOrders> orders_{};
    size_t order_count_{0};
};

} // namespace auction
} // namespace luv
