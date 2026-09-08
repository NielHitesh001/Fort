#include "luv_corporate_actions.hpp"
#include <cassert>
#include <cstdio>

void test_corporate_actions_adjustments() {
    luv::corporate_actions::CorporateActionsEngine engine;

    // Action 1: Symbol 1 (AAPL) 4-for-1 Stock Split
    assert(engine.register_action(luv::corporate_actions::CorporateActionRecord{
        .action_id = 1,
        .symbol_idx = 1,
        .action_type = luv::corporate_actions::CorporateActionType::kStockSplit,
        .ex_date_timestamp_ns = 1'000'000'000ULL,
        .dividend_amount_cash = 0,
        .split_numerator = 4,
        .split_denominator = 1
    }));

    // Action 2: Symbol 2 (MSFT) $0.75 Cash Dividend (7500 scaled)
    assert(engine.register_action(luv::corporate_actions::CorporateActionRecord{
        .action_id = 2,
        .symbol_idx = 2,
        .action_type = luv::corporate_actions::CorporateActionType::kCashDividend,
        .ex_date_timestamp_ns = 1'000'000'000ULL,
        .dividend_amount_cash = 7500,
        .split_numerator = 1,
        .split_denominator = 1
    }));

    // Test AAPL 4-for-1 split: Old limit order Buy 100 shares @ $400.00 (4000000)
    // Adjusted: Price = $100.00 (1000000), Qty = 400 shares
    auto aapl_adj = engine.adjust_order(1, 400'0000LL, 100);
    assert(aapl_adj.adjusted_price == 100'0000LL);
    assert(aapl_adj.adjusted_qty == 400);

    // Test MSFT $0.75 cash dividend: Old limit order Buy 100 shares @ $300.00 (3000000)
    // Adjusted: Price = $299.25 (2992500), Qty = 100 shares
    auto msft_adj = engine.adjust_order(2, 300'0000LL, 100);
    assert(msft_adj.adjusted_price == 299'2500LL);
    assert(msft_adj.adjusted_qty == 100);

    std::printf("[PASS] test_corporate_actions_adjustments (AAPL 4-for-1 -> $100/400sh, MSFT Div -> $299.25/100sh)\n");
}

int main() {
    test_corporate_actions_adjustments();
    std::printf("All corporate actions tests passed successfully.\n");
    return 0;
}
