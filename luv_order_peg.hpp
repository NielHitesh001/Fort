#pragma once

#include <cstdint>
#include <algorithm>

namespace luv {

enum class PegType : uint8_t {
    Midpoint = 0, // Peg to NBBO Midpoint ((BestBid + BestAsk) / 2)
    Primary  = 1, // Peg to same side of NBBO (Bid for Buy, Ask for Sell)
    Market   = 2  // Peg to opposite side of NBBO (Ask for Buy, Bid for Sell)
};

struct PeggedOrder {
    uint64_t order_id{0};
    bool is_buy{true};
    PegType peg_type{PegType::Midpoint};
    int64_t offset_ticks{0}; // In price units (e.g. +$0.01 or -$0.01)
    uint64_t limit_cap{0};   // Optional price cap (max price for Buy, min price for Sell; 0 = no cap)
    uint64_t quantity{0};
    uint64_t current_effective_price{0};
};

class PeggingEngine {
public:
    // Computes dynamic effective price of a pegged order given current NBBO
    static uint64_t compute_pegged_price(
        const PeggedOrder& order,
        uint64_t best_bid,
        uint64_t best_ask) noexcept
    {
        if (best_bid == 0 || best_ask == 0 || best_bid > best_ask) {
            return 0; // Invalid NBBO
        }

        int64_t base_price = 0;
        switch (order.peg_type) {
            case PegType::Midpoint:
                base_price = static_cast<int64_t>((best_bid + best_ask) / 2);
                break;
            case PegType::Primary:
                base_price = static_cast<int64_t>(order.is_buy ? best_bid : best_ask);
                break;
            case PegType::Market:
                base_price = static_cast<int64_t>(order.is_buy ? best_ask : best_bid);
                break;
        }

        int64_t calculated = base_price + order.offset_ticks;
        if (calculated <= 0) return 0;

        uint64_t final_price = static_cast<uint64_t>(calculated);

        // Apply limit price cap if set
        if (order.limit_cap > 0) {
            if (order.is_buy) {
                final_price = std::min(final_price, order.limit_cap);
            } else {
                final_price = std::max(final_price, order.limit_cap);
            }
        }

        return final_price;
    }

    // Returns true if price needs updating
    static bool update_order_price(
        PeggedOrder& order,
        uint64_t best_bid,
        uint64_t best_ask) noexcept
    {
        uint64_t new_price = compute_pegged_price(order, best_bid, best_ask);
        if (new_price != order.current_effective_price) {
            order.current_effective_price = new_price;
            return true;
        }
        return false;
    }
};

} // namespace luv
