#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {
namespace prime_brokerage {

struct AccountFinancingLedger {
    uint32_t account_id = 0;
    int64_t cash_debit_balance = 0;     // Positive = borrowing cash (pays debit interest)
    int64_t short_market_value = 0;     // Positive = short equity value (earns/pays borrow fee)
    double debit_interest_rate_annual = 0.065; // e.g. 6.50% (SOFR + 1.2%)
    double short_borrow_fee_annual = 0.015;    // e.g. 1.50% (Hard to borrow fee)
    int64_t accumulated_interest_payable = 0;  // Scaled x10,000
};

class MarginFinancingEngine {
public:
    static constexpr size_t kMaxAccounts = 64;

    MarginFinancingEngine() noexcept : num_accounts_(0) {}

    bool register_account(uint32_t account_id, double debit_rate, double borrow_fee) noexcept {
        if (num_accounts_ >= kMaxAccounts) return false;
        accounts_[num_accounts_++] = AccountFinancingLedger{
            .account_id = account_id,
            .cash_debit_balance = 0,
            .short_market_value = 0,
            .debit_interest_rate_annual = debit_rate,
            .short_borrow_fee_annual = borrow_fee,
            .accumulated_interest_payable = 0
        };
        return true;
    }

    AccountFinancingLedger* find_account(uint32_t account_id) noexcept {
        for (size_t i = 0; i < num_accounts_; ++i) {
            if (accounts_[i].account_id == account_id) return &accounts_[i];
        }
        return nullptr;
    }

    // Accrues daily interest (1/360 day count convention)
    int64_t accrue_daily_financing(uint32_t account_id, int64_t debit_balance, int64_t short_mv) noexcept {
        auto* acc = find_account(account_id);
        if (!acc) return 0;

        acc->cash_debit_balance = debit_balance;
        acc->short_market_value = short_mv;

        // Daily debit interest = DebitBalance * Rate / 360
        double daily_debit_interest = (static_cast<double>(debit_balance) * acc->debit_interest_rate_annual) / 360.0;
        // Daily borrow fee = ShortMV * BorrowFee / 360
        double daily_borrow_fee = (static_cast<double>(short_mv) * acc->short_borrow_fee_annual) / 360.0;

        int64_t total_daily_interest = static_cast<int64_t>(std::round(daily_debit_interest + daily_borrow_fee));
        acc->accumulated_interest_payable += total_daily_interest;

        return total_daily_interest;
    }

private:
    std::array<AccountFinancingLedger, kMaxAccounts> accounts_{};
    size_t num_accounts_{0};
};

} // namespace prime_brokerage
} // namespace luv
