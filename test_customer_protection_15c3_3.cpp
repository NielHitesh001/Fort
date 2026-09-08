#include <iostream>
#include <cassert>
#include "luv_customer_protection_15c3_3.hpp"

int main() {
    std::cout << "[TEST] Running SEC Rule 15c3-3 Customer Protection Reserve Rule Test...\n";

    luv::CustomerCredits credits;
    credits.free_credit_balances = 50'000'000;          // $50M Free credits
    credits.monies_borrowed_collateralized = 10'000'000; // $10M Monies borrowed against customer securities
    credits.monies_payable_customer_securities = 5'000'000;
    credits.customer_fail_to_receive = 2'000'000;
    credits.credit_balances_in_margin = 3'000'000;
    // Total Credits = $70,000,000

    luv::CustomerDebits debits;
    debits.margin_account_debits = 40'000'000; // $40M Margin debits (1% haircut = $400,000)
    debits.securities_borrowed_customer_short = 5'000'000;
    debits.customer_fail_to_deliver = 1'000'000;
    debits.drafts_for_immediate_credit = 4'000'000;
    // Gross Debits = $50,000,000
    // Allowable Debits = $50,000,000 - $400,000 = $49,600,000

    // Existing balance in Special Reserve Account = $20,000,000
    uint64_t existing_bank_reserve = 20'000'000;

    auto res = luv::CustomerProtectionCalculator::calculate_reserve(credits, debits, existing_bank_reserve);

    assert(res.total_credits == 70'000'000);
    assert(res.gross_debits == 50'000'000);
    assert(res.statutory_debit_haircut == 400'000);
    assert(res.allowable_debits == 49'600'000);

    // Required Reserve = $70,000,000 - $49,600,000 = $20,400,000
    assert(res.net_reserve_deposit_required == 20'400'000);

    // Existing is $20,000,000 -> Non-compliant, needs additional deposit of $400,000
    assert(!res.is_compliant);
    assert(res.additional_deposit_or_withdrawal == 400'000); // Deposit $400k

    // If bank balance is $25,000,000 -> Compliant, allows withdrawal of $4,600,000
    auto res_excess = luv::CustomerProtectionCalculator::calculate_reserve(credits, debits, 25'000'000);
    assert(res_excess.is_compliant);
    assert(res_excess.additional_deposit_or_withdrawal == -4'600'000); // May withdraw $4.6M

    std::cout << "[TEST] Credits: $" << res.total_credits
              << " | Allowable Debits: $" << res.allowable_debits
              << " | Required Reserve Deposit: $" << res.net_reserve_deposit_required << "\n";

    std::cout << "[TEST] SEC Rule 15c3-3 Customer Protection Reserve Rule Test Passed!\n";
    return 0;
}
