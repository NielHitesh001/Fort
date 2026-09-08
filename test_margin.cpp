#include "luv_margin.hpp"
#include <cassert>
#include <cstdio>

void test_cross_margin_lifecycle() {
    luv::margin::MarginConfig config;
    config.initial_margin_pct = 10.0;     // 10%
    config.maintenance_margin_pct = 5.0;  // 5%

    luv::margin::CrossMarginEngine engine(config);

    int64_t cash = 10'000; // $10,000 cash balance (in dollars / scaled)
    int64_t qty = 1000;    // 1,000 units
    int64_t entry_price = 1000000; // $100.00 entry (scaled x 10^4)
    // Initial position notional = (1000 * 1000000) / 10000 = $100,000 notional

    // 1. Healthy state: Market price = entry price ($100)
    auto state_healthy = engine.evaluate_account(cash, luv::exec::kBuy, qty, entry_price, entry_price);
    assert(state_healthy.status == luv::margin::MarginCallStatus::kHealthy);
    assert(state_healthy.equity == 10000);
    assert(state_healthy.initial_margin_required == 10000); // 10% of $100k = $10k
    assert(state_healthy.maintenance_margin_required == 5000); // 5% of $100k = $5k

    // 2. Warning state: Market price drops to $98 -> Unrealized loss = -$2,000 -> Equity = $8,000
    // Equity ($8k) < Initial Margin ($9.8k), but > Maintenance Margin ($4.9k)
    auto state_warning = engine.evaluate_account(cash, luv::exec::kBuy, qty, entry_price, 980000);
    assert(state_warning.status == luv::margin::MarginCallStatus::kWarning);
    assert(state_warning.equity == 8000);

    // 3. Liquidation state: Market price drops to $94 -> Unrealized loss = -$6,000 -> Equity = $4,000
    // Position notional = $94,000 -> MM required = $4,700 > Equity ($4,000) -> LIQUIDATION!
    auto state_liq = engine.evaluate_account(cash, luv::exec::kBuy, qty, entry_price, 940000);
    assert(state_liq.status == luv::margin::MarginCallStatus::kLiquidation);
    assert(state_liq.equity == 4000);

    // Create liquidation order
    uint8_t liq_side = 0;
    int64_t liq_qty = 0;
    assert(luv::margin::CrossMarginEngine::create_liquidation_order(luv::exec::kBuy, qty, liq_side, liq_qty));
    assert(liq_side == luv::exec::kSell); // Sell to close long
    assert(liq_qty == 1000);

    std::printf("[PASS] test_cross_margin_lifecycle\n");
}

int main() {
    test_cross_margin_lifecycle();
    std::printf("All cross margin and liquidation tests passed successfully.\n");
    return 0;
}
