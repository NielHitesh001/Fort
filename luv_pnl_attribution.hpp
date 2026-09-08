#pragma once

#include <cstdint>
#include <algorithm>

namespace luv {

struct PnlAttribution {
    int64_t realized_pnl{0};       // Realized trading PnL (in cents/fixed-point)
    int64_t unrealized_pnl{0};     // Mark-to-market unrealized PnL
    int64_t fee_rebate_pnl{0};     // Exchange fees / maker rebates
    int64_t financing_cost_pnl{0}; // Overnight / borrow financing costs
    int64_t total_pnl{0};          // Sum of all attribution components
    int64_t net_position{0};       // Current open quantity (long > 0, short < 0)
    uint64_t avg_cost_price{0};    // Weighted average open cost price (scaled x10,000)
};

class IntradayPnlAttributor {
public:
    IntradayPnlAttributor() noexcept = default;

    // Process a fill/trade execution
    void on_fill(bool is_buy, uint64_t price, uint64_t quantity, int64_t fee_rebate) noexcept {
        if (quantity == 0) return;

        pnl_.fee_rebate_pnl += fee_rebate; // Positive for rebates, negative for fees

        int64_t trade_qty = is_buy ? static_cast<int64_t>(quantity) : -static_cast<int64_t>(quantity);
        int64_t prev_pos = pnl_.net_position;

        if (prev_pos == 0) {
            // Opening fresh position
            pnl_.net_position = trade_qty;
            pnl_.avg_cost_price = price;
        } else if ((prev_pos > 0 && trade_qty > 0) || (prev_pos < 0 && trade_qty < 0)) {
            // Increasing existing position: update weighted average cost
            uint64_t total_qty = static_cast<uint64_t>(std::abs(prev_pos)) + quantity;
            pnl_.avg_cost_price = ((static_cast<uint64_t>(std::abs(prev_pos)) * pnl_.avg_cost_price) + (quantity * price)) / total_qty;
            pnl_.net_position += trade_qty;
        } else {
            // Reducing or flipping position: realize PnL on closed quantity
            uint64_t close_qty = std::min(static_cast<uint64_t>(std::abs(prev_pos)), quantity);
            
            int64_t price_diff = 0;
            if (prev_pos > 0) {
                // Was Long, Selling to close
                price_diff = static_cast<int64_t>(price) - static_cast<int64_t>(pnl_.avg_cost_price);
            } else {
                // Was Short, Buying to close
                price_diff = static_cast<int64_t>(pnl_.avg_cost_price) - static_cast<int64_t>(price);
            }

            // Realized PnL = price_diff * close_qty / 10,000 (scaled)
            pnl_.realized_pnl += (price_diff * static_cast<int64_t>(close_qty)) / 10000;

            if (quantity > static_cast<uint64_t>(std::abs(prev_pos))) {
                // Position flipped
                uint64_t rem_qty = quantity - static_cast<uint64_t>(std::abs(prev_pos));
                pnl_.net_position = is_buy ? static_cast<int64_t>(rem_qty) : -static_cast<int64_t>(rem_qty);
                pnl_.avg_cost_price = price;
            } else {
                pnl_.net_position += trade_qty;
                if (pnl_.net_position == 0) {
                    pnl_.avg_cost_price = 0;
                }
            }
        }

        recompute_total();
    }

    // Update current mark/fair price for unrealized mark-to-market PnL
    void on_mark_price(uint64_t mark_price) noexcept {
        if (pnl_.net_position == 0 || pnl_.avg_cost_price == 0) {
            pnl_.unrealized_pnl = 0;
        } else {
            int64_t price_diff = 0;
            if (pnl_.net_position > 0) {
                price_diff = static_cast<int64_t>(mark_price) - static_cast<int64_t>(pnl_.avg_cost_price);
            } else {
                price_diff = static_cast<int64_t>(pnl_.avg_cost_price) - static_cast<int64_t>(mark_price);
            }
            pnl_.unrealized_pnl = (price_diff * std::abs(pnl_.net_position)) / 10000;
        }

        recompute_total();
    }

    // Accrue financing / borrow cost
    void accrue_financing(int64_t cost) noexcept {
        pnl_.financing_cost_pnl -= cost; // Financing is an expense
        recompute_total();
    }

    const PnlAttribution& get_pnl() const noexcept { return pnl_; }

private:
    void recompute_total() noexcept {
        pnl_.total_pnl = pnl_.realized_pnl + pnl_.unrealized_pnl + pnl_.fee_rebate_pnl + pnl_.financing_cost_pnl;
    }

    PnlAttribution pnl_{};
};

} // namespace luv
