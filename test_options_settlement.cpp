#include "luv_options_settlement.hpp"
#include <cassert>
#include <cstdio>

void test_options_expiry_settlement() {
    luv::derivatives::OptionsSettlementEngine engine;

    // 1. Long AAPL Call $150 Strike, Cash Settled, 1 contract (100 shares)
    assert(engine.register_position(luv::derivatives::OptionContractPosition{
        .contract_id = 1,
        .underlying_symbol_idx = 1,
        .option_type = luv::derivatives::OptionType::kCall,
        .settlement_style = luv::derivatives::SettlementStyle::kCashSettled,
        .strike_price = 1500000, // $150.0000
        .contract_size = 100,
        .position_qty = 1
    }));

    // 2. Long AAPL Put $150 Strike, Physical Delivery, 1 contract (100 shares)
    assert(engine.register_position(luv::derivatives::OptionContractPosition{
        .contract_id = 2,
        .underlying_symbol_idx = 1,
        .option_type = luv::derivatives::OptionType::kPut,
        .settlement_style = luv::derivatives::SettlementStyle::kPhysicalDelivery,
        .strike_price = 1500000, // $150.0000
        .contract_size = 100,
        .position_qty = 1
    }));

    // Settle with AAPL closing at $160.0000 (1600000)
    // Call is ITM by $10 -> Cash PnL = $10 * 100 = $1,000 (1000 scaled)
    // Put is OTM -> Lapsed OTM
    std::array<luv::derivatives::OptionSettlementResult, 4> results{};
    size_t count = engine.settle_all(1600000, results.data(), results.size());

    assert(count == 2);
    assert(results[0].contract_id == 1);
    assert(results[0].action == luv::derivatives::SettlementAction::kExercisedCash);
    assert(results[0].cash_settlement_pnl == 1000);

    assert(results[1].contract_id == 2);
    assert(results[1].action == luv::derivatives::SettlementAction::kLapsedOTM);

    std::printf("[PASS] test_options_expiry_settlement (Call PnL: $%lld, Put: Lapsed)\n",
        static_cast<long long>(results[0].cash_settlement_pnl));
}

int main() {
    test_options_expiry_settlement();
    std::printf("All options settlement tests passed successfully.\n");
    return 0;
}
