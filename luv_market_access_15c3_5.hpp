#pragma once

#include <cstdint>
#include <array>
#include "luv_execution.hpp"

namespace luv {
namespace reg_15c3_5 {

enum class MarketAccessRejectReason : uint8_t {
    kNone = 0,
    kSingleOrderNotionalExceeded = 1,
    kSingleOrderQuantityExceeded = 2,
    kGrossCreditLimitExceeded = 3,
    kAccountBlocked = 4,
    kPriceBandViolation = 5
};

struct AccountCreditProfile {
    uint32_t account_id = 0;
    int64_t max_single_order_notional = 1'000'000'0000LL; // $1M max per order ($1M * 10000 = 10B)
    int64_t max_single_order_qty = 50'000;              // 50k shares max per order
    int64_t gross_credit_limit = 10'000'000'0000LL;       // $10M total gross credit limit ($10M * 10000 = 100B)
    int64_t accumulated_gross_notional = 0;
    bool is_blocked = false;
};

class MarketAccessRule15c35Validator {
public:
    static constexpr size_t kMaxAccounts = 64;

    MarketAccessRule15c35Validator() noexcept : num_accounts_(0) {}

    bool register_account(const AccountCreditProfile& profile) noexcept {
        if (num_accounts_ >= kMaxAccounts) return false;
        accounts_[num_accounts_++] = profile;
        return true;
    }

    AccountCreditProfile* find_account(uint32_t account_id) noexcept {
        for (size_t i = 0; i < num_accounts_; ++i) {
            if (accounts_[i].account_id == account_id) {
                return &accounts_[i];
            }
        }
        return nullptr;
    }

    // Pre-trade validation compliant with SEC 15c3-5 (Market Access Rule)
    MarketAccessRejectReason validate_and_reserve(uint32_t account_id, int64_t price, int64_t qty) noexcept {
        auto* acc = find_account(account_id);
        if (!acc) return MarketAccessRejectReason::kAccountBlocked;
        if (acc->is_blocked) return MarketAccessRejectReason::kAccountBlocked;

        if (qty > acc->max_single_order_qty) {
            return MarketAccessRejectReason::kSingleOrderQuantityExceeded;
        }

        // Notional = price * qty (with price scaled x10,000, notional is scaled x10,000)
        int64_t order_notional = (price * qty);
        if (order_notional > acc->max_single_order_notional) {
            return MarketAccessRejectReason::kSingleOrderNotionalExceeded;
        }

        if (acc->accumulated_gross_notional + order_notional > acc->gross_credit_limit) {
            return MarketAccessRejectReason::kGrossCreditLimitExceeded;
        }

        acc->accumulated_gross_notional += order_notional;
        return MarketAccessRejectReason::kNone;
    }

private:
    std::array<AccountCreditProfile, kMaxAccounts> accounts_{};
    size_t num_accounts_{0};
};

} // namespace reg_15c3_5
} // namespace luv
