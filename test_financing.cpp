#include "luv_financing.hpp"
#include <cassert>
#include <cstdio>

void test_prime_brokerage_financing_accrual() {
    luv::prime_brokerage::MarginFinancingEngine engine;

    // Account 101: Debit Rate 6.5% (0.065), Short Borrow Fee 1.5% (0.015)
    assert(engine.register_account(101, 0.065, 0.015));

    // Debit Balance: $1,000,000 (1000000 * 10000 = 10'000'000'000)
    // Short MV: $500,000 (500000 * 10000 = 5'000'000'000)
    // Daily Debit Interest = 10B * 0.065 / 360 = ~1,805,556
    // Daily Short Borrow Fee = 5B * 0.015 / 360 = ~208,333
    // Total Daily Accrual = ~2,013,889 (~$201.39)
    int64_t daily_interest = engine.accrue_daily_financing(101, 10'000'000'000LL, 5'000'000'000LL);
    assert(daily_interest > 2'000'000 && daily_interest < 2'050'000);

    auto* acc = engine.find_account(101);
    assert(acc != nullptr);
    assert(acc->accumulated_interest_payable == daily_interest);

    std::printf("[PASS] test_prime_brokerage_financing_accrual (Daily Interest: $%.2f)\n",
        static_cast<double>(daily_interest) / 10000.0);
}

int main() {
    test_prime_brokerage_financing_accrual();
    std::printf("All prime brokerage margin financing tests passed successfully.\n");
    return 0;
}
