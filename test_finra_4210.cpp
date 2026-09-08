#include <iostream>
#include <cassert>
#include "luv_finra_4210.hpp"

int main() {
    std::cout << "[TEST] Running FINRA Rule 4210 Option Strategy Margin Test...\n";

    // 1. Naked Short Put: Underlying $100.00 (1,000,000), Strike $95.00 (950,000), Premium $2.00 (20,000), 1 Contract (100 shares)
    // Premium value = $200
    // Stock value = $10,000
    // 20% of stock = $2,000
    // OTM amount = ($100 - $95) * 100 = $500
    // Base add = $2,000 - $500 = $1,500 (> 10% min of $1,000)
    // Margin Required = $200 (premium) + $1,500 = $1,700
    auto naked_put = luv::Finra4210MarginCalculator::calculate_naked_option(
        luv::OptionLegType::ShortPut, 1'000'000, 950'000, 20'000, 1
    );
    assert(naked_put.recognized_strategy == luv::SpreadStrategy::NakedOption);
    assert(naked_put.initial_margin_required == 1700);

    // 2. Covered Call: 100 shares of stock @ $100.00 ($10,000 val) + Short 1 Call @ $105 strike
    // Margin = Reg T 50% on stock = $5,000; 0 additional on short call
    auto cov_call = luv::Finra4210MarginCalculator::calculate_covered_call(100, 1'000'000, 1'050'000, 15'000, 1);
    assert(cov_call.recognized_strategy == luv::SpreadStrategy::CoveredCall);
    assert(cov_call.initial_margin_required == 5000);
    assert(cov_call.maintenance_margin_required == 2500);

    // 3. Vertical Credit Spread: Strike width = $5.00 ($95 / $100) x 10 contracts (1,000 shares)
    // Margin = $5.00 * 1,000 = $5,000
    luv::OptionLeg leg1{luv::OptionLegType::ShortCall, 1'000'000, 30'000, 10};
    luv::OptionLeg leg2{luv::OptionLegType::LongCall, 1'050'000, 10'000, 10};
    auto vert = luv::Finra4210MarginCalculator::calculate_vertical_spread(leg1, leg2);
    assert(vert.recognized_strategy == luv::SpreadStrategy::VerticalSpread);
    assert(vert.initial_margin_required == 5000);

    // 4. Iron Condor: Put Wing $90/$95 ($5 width), Call Wing $105/$110 ($5 width) x 5 contracts
    // Margin = max($5, $5) * 500 shares = $2,500
    auto condor = luv::Finra4210MarginCalculator::calculate_iron_condor(900'000, 950'000, 1'050'000, 1'100'000, 5);
    assert(condor.recognized_strategy == luv::SpreadStrategy::IronCondor);
    assert(condor.initial_margin_required == 2500);

    std::cout << "[TEST] Naked Put Margin: $" << naked_put.initial_margin_required
              << " | Covered Call Margin: $" << cov_call.initial_margin_required
              << " | Vertical Spread Margin: $" << vert.initial_margin_required
              << " | Iron Condor Margin: $" << condor.initial_margin_required << "\n";

    std::cout << "[TEST] FINRA Rule 4210 Option Strategy Margin Test Passed!\n";
    return 0;
}
