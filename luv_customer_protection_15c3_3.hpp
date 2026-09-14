#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace luv {

struct CustomerCredits {
    uint64_t free_credit_balances{0};          // Item 1: Free credit balances in customer security accounts
    uint64_t monies_borrowed_collateralized{0}; // Item 2: Monies borrowed collateralized by customer securities
    uint64_t monies_payable_customer_securities{0}; // Item 3: Monies payable against customer securities loaned
    uint64_t customer_fail_to_receive{0};       // Item 4: Customers' securities failed to receive
    uint64_t credit_balances_in_margin{0};      // Item 5: Credit balances in firm accounts attributable to customer orders
};

struct CustomerDebits {
    uint64_t margin_account_debits{0};          // Item 10: Customer margin debit balances (reduced by 1% statutory haircut)
    uint64_t securities_borrowed_customer_short{0}; // Item 11: Securities borrowed to effectuate customer shorts
    uint64_t customer_fail_to_deliver{0};       // Item 12: Customer securities failed to deliver (under 30 days old)
    uint64_t drafts_for_immediate_credit{0};   // Item 13: Margin required by clearing agencies for customer transactions
};

struct ReserveRequirementResult {
    bool arithmetic_valid{false};
    uint64_t total_credits{0};
    uint64_t gross_debits{0};
    uint64_t statutory_debit_haircut{0}; // 1% deduction on margin debits
    uint64_t allowable_debits{0};
    uint64_t net_reserve_deposit_required{0}; // Excess of Total Credits over Allowable Debits
    uint64_t existing_reserve_bank_balance{0};
    int64_t additional_deposit_or_withdrawal{0}; // Positive = must deposit, Negative = allowable withdrawal
    bool is_compliant{false};
};

class CustomerProtectionCalculator {
public:
    static ReserveRequirementResult calculate_reserve(
        const CustomerCredits& credits,
        const CustomerDebits& debits,
        uint64_t existing_reserve_bank_balance) noexcept
    {
        ReserveRequirementResult res;
        res.existing_reserve_bank_balance = existing_reserve_bank_balance;

        // 1. Total Credits
        const uint64_t credit_items[] = {credits.free_credit_balances,
            credits.monies_borrowed_collateralized, credits.monies_payable_customer_securities,
            credits.customer_fail_to_receive, credits.credit_balances_in_margin};
        for (uint64_t item : credit_items) {
            if (__builtin_add_overflow(res.total_credits, item, &res.total_credits)) return {};
        }

        // 2. Total Debits & 1% statutory margin debit reduction (Item 10 haircut)
        const uint64_t debit_items[] = {debits.margin_account_debits,
            debits.securities_borrowed_customer_short, debits.customer_fail_to_deliver,
            debits.drafts_for_immediate_credit};
        for (uint64_t item : debit_items) {
            if (__builtin_add_overflow(res.gross_debits, item, &res.gross_debits)) return {};
        }

        res.statutory_debit_haircut = (debits.margin_account_debits * 1) / 100; // 1% haircut

        res.allowable_debits = (res.gross_debits > res.statutory_debit_haircut) ?
                               (res.gross_debits - res.statutory_debit_haircut) : 0;

        // 3. Required Reserve Balance = max(0, Total Credits - Allowable Debits)
        if (res.total_credits > res.allowable_debits) {
            res.net_reserve_deposit_required = res.total_credits - res.allowable_debits;
        } else {
            res.net_reserve_deposit_required = 0;
        }

        // 4. Deposit vs Withdrawal calculation
        // If required > existing => must deposit (positive)
        // If existing > required => may withdraw (negative)
        if (res.net_reserve_deposit_required >= existing_reserve_bank_balance) {
            const uint64_t difference = res.net_reserve_deposit_required - existing_reserve_bank_balance;
            if (difference > static_cast<uint64_t>(INT64_MAX)) return {};
            res.additional_deposit_or_withdrawal = static_cast<int64_t>(difference);
        } else {
            const uint64_t difference = existing_reserve_bank_balance - res.net_reserve_deposit_required;
            const uint64_t negative_limit = static_cast<uint64_t>(INT64_MAX) + 1U;
            if (difference > negative_limit) return {};
            res.additional_deposit_or_withdrawal = difference == negative_limit
                ? INT64_MIN : -static_cast<int64_t>(difference);
        }

        res.is_compliant = (existing_reserve_bank_balance >= res.net_reserve_deposit_required);

        res.arithmetic_valid = true;
        return res;
    }
};

} // namespace luv
