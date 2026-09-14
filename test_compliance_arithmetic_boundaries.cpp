#include "luv_net_capital_15c3_1.hpp"
#include "luv_customer_protection_15c3_3.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace luv;
    auto capital = NetCapitalCalculator::calculate(UINT64_MAX, 0, UINT64_MAX, nullptr, 0);
    assert(capital.arithmetic_valid && capital.is_compliant);
    assert(capital.required_minimum_net_capital == UINT64_MAX / 15U);
    NetCapitalPosition positions[2]{};
    positions[0].is_non_marketable = true;
    positions[0].market_value_usd = UINT64_MAX;
    positions[1] = positions[0];
    capital = NetCapitalCalculator::calculate(UINT64_MAX, 0, 0, positions, 2);
    assert(!capital.arithmetic_valid && !capital.is_compliant);
    assert(!NetCapitalCalculator::calculate(0, 0, 0, nullptr, 1).arithmetic_valid);
    positions[0].is_non_marketable = false;
    capital = NetCapitalCalculator::calculate(UINT64_MAX, 0, 0, positions, 1);
    assert(capital.arithmetic_valid && capital.total_haircuts > UINT64_MAX / 10U);

    CustomerCredits credits{};
    CustomerDebits debits{};
    auto reserve = CustomerProtectionCalculator::calculate_reserve(credits, debits, 0);
    assert(reserve.arithmetic_valid && reserve.is_compliant);
    credits.free_credit_balances = UINT64_MAX;
    reserve = CustomerProtectionCalculator::calculate_reserve(credits, debits, UINT64_MAX);
    assert(reserve.arithmetic_valid && reserve.additional_deposit_or_withdrawal == 0);
    credits.credit_balances_in_margin = 1;
    assert(!CustomerProtectionCalculator::calculate_reserve(credits, debits, 0).arithmetic_valid);
    credits = {};
    debits.margin_account_debits = UINT64_MAX;
    debits.customer_fail_to_deliver = 1;
    assert(!CustomerProtectionCalculator::calculate_reserve(credits, debits, 0).arithmetic_valid);
    debits = {};
    reserve = CustomerProtectionCalculator::calculate_reserve(credits, debits,
        static_cast<uint64_t>(INT64_MAX) + 1U);
    assert(reserve.arithmetic_valid && reserve.additional_deposit_or_withdrawal == INT64_MIN);
    assert(!CustomerProtectionCalculator::calculate_reserve(credits, debits, UINT64_MAX).arithmetic_valid);
    credits.free_credit_balances = INT64_MAX;
    reserve = CustomerProtectionCalculator::calculate_reserve(credits, debits, 0);
    assert(reserve.arithmetic_valid && reserve.additional_deposit_or_withdrawal == INT64_MAX);
    ++credits.free_credit_balances;
    assert(!CustomerProtectionCalculator::calculate_reserve(credits, debits, 0).arithmetic_valid);
    std::puts("Compliance arithmetic boundary tests passed.");
}
