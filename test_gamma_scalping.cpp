#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_gamma_scalping.hpp"

int main() {
    std::cout << "[TEST] Running Delta-Neutral Gamma Scalping Controller Test...\n";

    luv::GammaScalpParams params;
    params.delta_rebalance_threshold = 5.0; // Rebalance when net delta exceeds +/- 5 shares
    params.fee_per_share = 0.005;

    luv::GammaScalpingController controller(params);

    // 1. Initialize Long Call position: +50 Delta @ $100.00 spot
    // Initial hedge: short 50 shares
    controller.initialize(50.0, 100.0);
    const auto& s1 = controller.get_state();
    assert(s1.underlying_shares == -50);
    assert(std::abs(s1.net_portfolio_delta) < 1e-4);

    // 2. Spot moves to $105.00 -> Options delta increases from 50 to 62 due to positive Gamma
    // Net delta = -50 + 62 = +12 (> 5 threshold) -> Triggers rebalance to short 62 shares (-12 shares traded)
    bool rebalanced = controller.on_market_update(105.0, 62.0);
    assert(rebalanced);
    const auto& s2 = controller.get_state();
    assert(s2.underlying_shares == -62);
    assert(s2.rebalance_count == 1);
    assert(s2.last_rebalance_price == 105.0);

    // 3. Spot drops back to $98.00 -> Options delta decreases to 40
    // Net delta = -62 + 40 = -22 (< -5 threshold) -> Triggers rebalance to short 40 shares (+22 shares bought back cheap)
    rebalanced = controller.on_market_update(98.0, 40.0);
    assert(rebalanced);
    const auto& s3 = controller.get_state();
    assert(s3.underlying_shares == -40);
    assert(s3.rebalance_count == 2);
    // Bought back at $98 after selling at $105 -> positive gamma scalping gains
    assert(s3.total_realized_gamma_pnl > 0.0);

    std::cout << "[TEST] Rebalances: " << s3.rebalance_count
              << " | Realized Gamma Scalp PnL: $" << s3.total_realized_gamma_pnl
              << " | Fees: $" << s3.total_transaction_costs << "\n";

    std::cout << "[TEST] Delta-Neutral Gamma Scalping Controller Test Passed!\n";
    return 0;
}
