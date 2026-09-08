#pragma once

#include <cstdint>
#include <vector>
#include "luv_execution.hpp"

namespace luv {

enum class SpreadLegState : uint8_t {
    kPending = 0,
    kLeg1Filled = 1,
    kFullyHedeged = 2,
    kLeggingUnwind = 3,
};

struct SpreadOrder {
    uint32_t spread_id = 0;
    uint16_t leg1_symbol = 0;
    uint8_t  leg1_side = exec::kBuy;
    int64_t  leg1_qty = 100;
    int64_t  leg1_limit_price = 0;

    uint16_t leg2_symbol = 1;
    uint8_t  leg2_side = exec::kSell;
    int64_t  leg2_ratio = 1; // 1:1 ratio
    int64_t  leg2_limit_price = 0;

    uint64_t legging_timeout_ns = 50'000'000; // 50ms legging risk limit
    SpreadLegState state = SpreadLegState::kPending;
    uint64_t leg1_filled_timestamp_ns = 0;
};

class MultiLegSpreadEngine {
public:
    static exec::OrderIntent generate_leg1_intent(const SpreadOrder& spread, uint64_t now_ns) noexcept {
        exec::OrderIntent intent{};
        intent.symbol_idx = spread.leg1_symbol;
        intent.side = spread.leg1_side;
        intent.qty = spread.leg1_qty;
        intent.price = spread.leg1_limit_price;
        intent.client_order_id = spread.spread_id * 10 + 1;
        intent.now_ns = now_ns;
        intent.alpha_timestamp_ns = now_ns;
        return intent;
    }

    static exec::OrderIntent on_leg1_fill(SpreadOrder& spread, int64_t filled_qty, uint64_t now_ns) noexcept {
        spread.state = SpreadLegState::kLeg1Filled;
        spread.leg1_filled_timestamp_ns = now_ns;

        // Auto-generate compensating Leg 2 hedge order
        exec::OrderIntent leg2_intent{};
        leg2_intent.symbol_idx = spread.leg2_symbol;
        leg2_intent.side = spread.leg2_side;
        leg2_intent.qty = filled_qty * spread.leg2_ratio;
        leg2_intent.price = spread.leg2_limit_price;
        leg2_intent.client_order_id = spread.spread_id * 10 + 2;
        leg2_intent.now_ns = now_ns;
        leg2_intent.alpha_timestamp_ns = now_ns;
        return leg2_intent;
    }

    [[nodiscard]] static bool check_legging_timeout(const SpreadOrder& spread, uint64_t now_ns) noexcept {
        if (spread.state == SpreadLegState::kLeg1Filled) {
            return (now_ns > spread.leg1_filled_timestamp_ns &&
                   (now_ns - spread.leg1_filled_timestamp_ns) > spread.legging_timeout_ns);
        }
        return false;
    }

    static exec::OrderIntent generate_emergency_unwind(SpreadOrder& spread, uint64_t now_ns) noexcept {
        spread.state = SpreadLegState::kLeggingUnwind;
        exec::OrderIntent unwind_intent{};
        unwind_intent.symbol_idx = spread.leg1_symbol;
        // Invert side to close Leg 1 position
        unwind_intent.side = (spread.leg1_side == exec::kBuy) ? exec::kSell : exec::kBuy;
        unwind_intent.qty = spread.leg1_qty;
        unwind_intent.price = (unwind_intent.side == exec::kSell) ? 1 : 10'000'000 * 10'000LL; // Aggressive market/crossing price
        unwind_intent.client_order_id = spread.spread_id * 10 + 9;
        unwind_intent.now_ns = now_ns;
        return unwind_intent;
    }
};

} // namespace luv
