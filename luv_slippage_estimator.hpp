#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

struct BookLevel {
    uint64_t price{0}; // Scaled x10,000 ($100.00 = 1,000,000)
    uint64_t quantity{0};
};

struct SlippageEstimate {
    uint64_t total_filled_qty{0};
    uint64_t total_cost_cents{0};
    uint64_t vwap_exec_price{0};
    uint64_t best_initial_price{0};
    int64_t price_slippage_bps{0}; // (VWAP - BestPrice) / BestPrice * 10,000 (in BPS)
    uint64_t levels_consumed{0};
    bool fully_filled{true};
};

class SlippageEstimator {
public:
    static constexpr size_t kMaxDepthLevels = 32;

    // Estimates slippage and average fill price across order book levels for order of size order_qty
    static SlippageEstimate estimate_slippage(
        bool is_buy,
        uint64_t order_qty,
        const BookLevel* levels,
        size_t level_count) noexcept
    {
        SlippageEstimate est;
        if (order_qty == 0 || !levels || level_count == 0) {
            est.fully_filled = false;
            return est;
        }

        est.best_initial_price = levels[0].price;
        uint64_t rem_qty = order_qty;
        uint64_t total_notional = 0; // price * qty (scaled x10,000)

        for (size_t i = 0; i < level_count && rem_qty > 0; ++i) {
            const auto& lvl = levels[i];
            if (lvl.quantity == 0 || lvl.price == 0) continue;

            uint64_t fill = std::min(rem_qty, lvl.quantity);
            total_notional += fill * lvl.price;
            rem_qty -= fill;
            est.total_filled_qty += fill;
            ++est.levels_consumed;
        }

        if (est.total_filled_qty > 0) {
            est.vwap_exec_price = total_notional / est.total_filled_qty;
            est.total_cost_cents = total_notional / 10000;

            // Calculate slippage in basis points
            if (est.best_initial_price > 0) {
                int64_t diff = 0;
                if (is_buy) {
                    diff = static_cast<int64_t>(est.vwap_exec_price) - static_cast<int64_t>(est.best_initial_price);
                } else {
                    diff = static_cast<int64_t>(est.best_initial_price) - static_cast<int64_t>(est.vwap_exec_price);
                }
                est.price_slippage_bps = (diff * 10000) / static_cast<int64_t>(est.best_initial_price);
            }
        }

        est.fully_filled = (rem_qty == 0);
        return est;
    }
};

} // namespace luv
