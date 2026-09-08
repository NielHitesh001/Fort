#include <iostream>
#include <cassert>
#include "luv_net_capital_15c3_1.hpp"

int main() {
    std::cout << "[TEST] Running SEC Rule 15c3-1 Net Capital Rule Test...\n";

    // Setup Broker-Dealer Balance Sheet:
    // Total Assets: $10,000,000
    // Non-Allowable Assets (unsecured loans, fixed assets): $1,000,000
    // Tentative Net Capital = $9,000,000
    // Aggregate Indebtedness = $30,000,000 (6 2/3% = $2,000,000)
    uint64_t total_assets = 10'000'000;
    uint64_t non_allowable = 1'000'000;
    uint64_t ai = 30'000'000;

    luv::NetCapitalPosition positions[3];
    // Position 1: $1,000,000 in equity (15% haircut = $150,000, >10% of $9M ($900k) by $100k -> undue conc +15k = $165,000)
    positions[0].symbol_id = 1;
    positions[0].market_value_usd = 1'000'000;
    positions[0].is_equity = true;

    // Position 2: $2,000,000 in 10-year US Treasuries (6% haircut = $120,000)
    positions[1].symbol_id = 2;
    positions[1].market_value_usd = 2'000'000;
    positions[1].is_equity = false;
    positions[1].is_treasury = true;
    positions[1].treasury_maturity_years = 10;

    // Position 3: $100,000 in non-marketable private securities (100% deduction = $100,000)
    positions[2].symbol_id = 3;
    positions[2].market_value_usd = 100'000;
    positions[2].is_non_marketable = true;

    auto res = luv::NetCapitalCalculator::calculate(
        total_assets, non_allowable, ai, positions, 3, luv::NetCapitalStandard::AggregateIndebtedness
    );

    assert(res.tentative_net_capital == 9'000'000);
    assert(res.total_haircuts == (165'000 + 120'000 + 100'000)); // $385,000
    assert(res.net_capital == (9'000'000 - 385'000));            // $8,615,000
    assert(res.required_minimum_net_capital == 2'000'000);        // 6 2/3% of $30M = $2M (> $250k)
    assert(res.is_compliant);
    assert(!res.is_early_warning); // $8.615M > $2.4M early warning
    assert(res.excess_net_capital == (8'615'000 - 2'000'000));

    // Test Early Warning / Deficiency scenario:
    // AI = $120,000,000 -> Requirement = $8,000,000. Net Capital $8.615M < $9.6M early warning (120%)
    auto res_ew = luv::NetCapitalCalculator::calculate(
        total_assets, non_allowable, 120'000'000, positions, 3, luv::NetCapitalStandard::AggregateIndebtedness
    );
    assert(res_ew.required_minimum_net_capital == 8'000'000);
    assert(res_ew.is_compliant);
    assert(res_ew.is_early_warning); // Early warning triggered

    std::cout << "[TEST] Net Capital: $" << res.net_capital
              << " | Required Min: $" << res.required_minimum_net_capital
              << " | Excess: $" << res.excess_net_capital << "\n";

    std::cout << "[TEST] SEC Rule 15c3-1 Net Capital Rule Test Passed!\n";
    return 0;
}
