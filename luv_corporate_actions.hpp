#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace corporate_actions {

enum class CorporateActionType : uint8_t {
    kCashDividend = 0,
    kStockSplit = 1,
    kReverseStockSplit = 2,
    kSpinOff = 3
};

struct CorporateActionRecord {
    uint64_t action_id = 0;
    uint16_t symbol_idx = 0;
    CorporateActionType action_type = CorporateActionType::kCashDividend;
    uint64_t ex_date_timestamp_ns = 0;
    int64_t dividend_amount_cash = 0; // Scaled x10,000 (e.g. $0.50 = 5000)
    int32_t split_numerator = 1;     // e.g. 2 for 2-for-1 split
    int32_t split_denominator = 1;   // e.g. 1
};

struct AdjustedPriceQty {
    int64_t adjusted_price = 0; // Scaled x10,000
    int64_t adjusted_qty = 0;
};

class CorporateActionsEngine {
public:
    static constexpr size_t kMaxActions = 64;

    CorporateActionsEngine() noexcept : num_actions_(0) {}

    bool register_action(const CorporateActionRecord& action) noexcept {
        if (num_actions_ >= kMaxActions) return false;
        actions_[num_actions_++] = action;
        return true;
    }

    // Adjusts unexecuted limit order price and quantity across an ex-date boundary
    AdjustedPriceQty adjust_order(uint16_t sym, int64_t unadjusted_price, int64_t unadjusted_qty) const noexcept {
        AdjustedPriceQty res{unadjusted_price, unadjusted_qty};

        for (size_t i = 0; i < num_actions_; ++i) {
            const auto& act = actions_[i];
            if (act.symbol_idx != sym) continue;

            if (act.action_type == CorporateActionType::kCashDividend) {
                // Drop limit price by dividend amount
                res.adjusted_price = std::max<int64_t>(1, res.adjusted_price - act.dividend_amount_cash);
            } else if (act.action_type == CorporateActionType::kStockSplit ||
                       act.action_type == CorporateActionType::kReverseStockSplit) {
                if (act.split_numerator > 0 && act.split_denominator > 0) {
                    // New Price = Old Price * Denom / Num
                    res.adjusted_price = (res.adjusted_price * act.split_denominator) / act.split_numerator;
                    // New Qty = Old Qty * Num / Denom
                    res.adjusted_qty = (res.adjusted_qty * act.split_numerator) / act.split_denominator;
                }
            }
        }

        return res;
    }

private:
    std::array<CorporateActionRecord, kMaxActions> actions_{};
    size_t num_actions_{0};
};

} // namespace corporate_actions
} // namespace luv
