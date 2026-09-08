#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace booking {

struct SubAccountAllocationTarget {
    uint32_t sub_account_id = 0;
    double target_weight = 0.0; // e.g. 0.40 for 40%
};

struct AllocatedTradeRecord {
    uint32_t sub_account_id = 0;
    int64_t allocated_qty = 0;
    int64_t avg_price = 0;
};

class TradeAllocationEngine {
public:
    static constexpr size_t kMaxSubAccounts = 32;

    TradeAllocationEngine() noexcept : num_targets_(0) {}

    bool register_target(uint32_t sub_account_id, double target_weight) noexcept {
        if (num_targets_ >= kMaxSubAccounts || target_weight <= 0.0) return false;
        targets_[num_targets_++] = SubAccountAllocationTarget{sub_account_id, target_weight};
        return true;
    }

    // Pro-rata allocation of parent execution fill across sub-accounts
    size_t allocate_block(int64_t parent_fill_qty, int64_t avg_exec_price,
                          AllocatedTradeRecord* out_allocations, size_t max_out) noexcept {
        if (!out_allocations || max_out == 0 || num_targets_ == 0 || parent_fill_qty <= 0) return 0;

        int64_t remaining_shares = parent_fill_qty;
        size_t count = 0;

        for (size_t i = 0; i < num_targets_ && count < max_out; ++i) {
            int64_t sub_qty = static_cast<int64_t>(parent_fill_qty * targets_[i].target_weight);
            if (i == num_targets_ - 1) {
                // Assign leftover rounding shares to last account
                sub_qty = remaining_shares;
            }

            out_allocations[count++] = AllocatedTradeRecord{
                .sub_account_id = targets_[i].sub_account_id,
                .allocated_qty = sub_qty,
                .avg_price = avg_exec_price
            };

            remaining_shares -= sub_qty;
        }

        return count;
    }

private:
    std::array<SubAccountAllocationTarget, kMaxSubAccounts> targets_{};
    size_t num_targets_{0};
};

} // namespace booking
} // namespace luv
