#include "luv_collateral.hpp"
#include <cassert>
#include <cstdio>

void test_collateral_haircuts_and_valuation() {
    luv::collateral::CollateralManagementEngine engine;

    // 1. Deposit $50,000 USD Cash (0% Haircut)
    assert(engine.deposit_asset(luv::collateral::CollateralType::kCashUSD, 50000, 1.0, 0.0));

    // 2. Deposit $100,000 US Treasuries (2% Haircut -> $98,000 effective)
    assert(engine.deposit_asset(luv::collateral::CollateralType::kUsTreasury, 100000, 1.0, 2.0));

    // 3. Deposit 1,000 shares of Apple @ $150 = $150,000 (15% Haircut -> $127,500 effective)
    assert(engine.deposit_asset(luv::collateral::CollateralType::kEquitiesTier1, 1000, 150.0, 15.0));

    // Total Effective Collateral = $50,000 + $98,000 + $127,500 = $275,500
    int64_t total_eff = engine.compute_effective_collateral_usd();
    assert(total_eff == 275500);

    // Partial withdrawal of Treasuries ($50,000 withdrawn)
    assert(engine.withdraw_asset(luv::collateral::CollateralType::kUsTreasury, 50000));

    // Total Effective Collateral = $50,000 + $49,000 + $127,500 = $226,500
    total_eff = engine.compute_effective_collateral_usd();
    assert(total_eff == 226500);

    std::printf("[PASS] test_collateral_haircuts_and_valuation (Effective Collateral: $%lld)\n",
        static_cast<long long>(total_eff));
}

int main() {
    test_collateral_haircuts_and_valuation();
    std::printf("All collateral management & haircut tests passed successfully.\n");
    return 0;
}
