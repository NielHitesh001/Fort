#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace compliance {

struct LargeTraderActivity {
    uint32_t account_id = 0;
    uint64_t ltid = 0;                  // Large Trader ID (SEC 13H assigned)
    int64_t daily_share_volume = 0;
    int64_t daily_dollar_volume = 0;    // Scaled x10,000
    int64_t monthly_share_volume = 0;
    int64_t monthly_dollar_volume = 0;  // Scaled x10,000
    bool is_large_trader_triggered = false;
};

class Rule13hLargeTraderEngine {
public:
    static constexpr size_t kMaxAccounts = 64;
    // Identifying Activity Level thresholds:
    // Daily: 2,000,000 shares OR $20,000,000
    // Monthly: 20,000,000 shares OR $200,000,000
    static constexpr int64_t kDailyShareThreshold = 2'000'000;
    static constexpr int64_t kDailyDollarThreshold = 20'000'000'0000LL;   // $20M * 10,000
    static constexpr int64_t kMonthlyShareThreshold = 20'000'000;
    static constexpr int64_t kMonthlyDollarThreshold = 200'000'000'0000LL; // $200M * 10,000

    Rule13hLargeTraderEngine() noexcept : num_accounts_(0) {}

    bool register_account(uint32_t account_id, uint64_t ltid = 0) noexcept {
        if (num_accounts_ >= kMaxAccounts) return false;
        accounts_[num_accounts_++] = LargeTraderActivity{
            .account_id = account_id,
            .ltid = ltid,
            .daily_share_volume = 0,
            .daily_dollar_volume = 0,
            .monthly_share_volume = 0,
            .monthly_dollar_volume = 0,
            .is_large_trader_triggered = (ltid != 0)
        };
        return true;
    }

    LargeTraderActivity* find_account(uint32_t account_id) noexcept {
        for (size_t i = 0; i < num_accounts_; ++i) {
            if (accounts_[i].account_id == account_id) return &accounts_[i];
        }
        return nullptr;
    }

    // Records trade execution and evaluates SEC 13H Large Trader identification thresholds
    bool record_execution(uint32_t account_id, int64_t price, int64_t qty) noexcept {
        auto* acc = find_account(account_id);
        if (!acc) return false;

        int64_t dollar_val = price * qty;
        acc->daily_share_volume += qty;
        acc->daily_dollar_volume += dollar_val;
        acc->monthly_share_volume += qty;
        acc->monthly_dollar_volume += dollar_val;

        // Check identifying activity level breach
        if (acc->daily_share_volume >= kDailyShareThreshold ||
            acc->daily_dollar_volume >= kDailyDollarThreshold ||
            acc->monthly_share_volume >= kMonthlyShareThreshold ||
            acc->monthly_dollar_volume >= kMonthlyDollarThreshold) {
            acc->is_large_trader_triggered = true;
        }

        return true;
    }

    bool is_large_trader(uint32_t account_id) const noexcept {
        for (size_t i = 0; i < num_accounts_; ++i) {
            if (accounts_[i].account_id == account_id) {
                return accounts_[i].is_large_trader_triggered;
            }
        }
        return false;
    }

private:
    std::array<LargeTraderActivity, kMaxAccounts> accounts_{};
    size_t num_accounts_{0};
};

} // namespace compliance
} // namespace luv
