#include <iostream>
#include <cassert>
#include "luv_pnl_attribution.hpp"

int main() {
    std::cout << "[TEST] Running Intraday PnL Attribution Engine Test...\n";

    luv::IntradayPnlAttributor pnl;

    // 1. Buy 100 shares @ $100.00 (1,000,000) with -$1.50 fee (-15000 scaled or -$1)
    pnl.on_fill(true, 1'000'000, 100, -2);
    assert(pnl.get_pnl().net_position == 100);
    assert(pnl.get_pnl().avg_cost_price == 1'000'000);
    assert(pnl.get_pnl().realized_pnl == 0);
    assert(pnl.get_pnl().fee_rebate_pnl == -2);

    // 2. Buy another 100 shares @ $110.00 (1,100,000)
    pnl.on_fill(true, 1'100'000, 100, -2);
    assert(pnl.get_pnl().net_position == 200);
    assert(pnl.get_pnl().avg_cost_price == 1'050'000); // Average cost $105.00

    // 3. Mark price moves to $115.00 (1,150,000)
    pnl.on_mark_price(1'150'000);
    // Unrealized = ($115 - $105) * 200 = $10 * 200 = $2,000
    assert(pnl.get_pnl().unrealized_pnl == 2000);

    // 4. Sell 100 shares @ $120.00 (1,200,000) with +$1 maker rebate
    pnl.on_fill(false, 1'200'000, 100, 1);
    // Realized = ($120 - $105) * 100 = $15 * 100 = $1,500
    assert(pnl.get_pnl().realized_pnl == 1500);
    assert(pnl.get_pnl().net_position == 100);
    assert(pnl.get_pnl().avg_cost_price == 1'050'000);

    // 5. Accrue overnight financing cost of $50
    pnl.accrue_financing(50);
    assert(pnl.get_pnl().financing_cost_pnl == -50);

    // Mark price still $115 for remaining 100 shares: Unrealized = ($115 - $105) * 100 = $1,000
    pnl.on_mark_price(1'150'000);
    assert(pnl.get_pnl().unrealized_pnl == 1000);

    // Total = Realized(1500) + Unrealized(1000) + Fees(-3) + Financing(-50) = 2447
    assert(pnl.get_pnl().total_pnl == 2447);

    std::cout << "[TEST] Realized: $" << pnl.get_pnl().realized_pnl
              << " | Unrealized: $" << pnl.get_pnl().unrealized_pnl
              << " | Fees: $" << pnl.get_pnl().fee_rebate_pnl
              << " | Financing: $" << pnl.get_pnl().financing_cost_pnl
              << " | Total: $" << pnl.get_pnl().total_pnl << "\n";

    std::cout << "[TEST] Intraday PnL Attribution Engine Test Passed!\n";
    return 0;
}
