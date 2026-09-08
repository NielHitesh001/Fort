#include <iostream>
#include <cassert>
#include "luv_slippage_estimator.hpp"

int main() {
    std::cout << "[TEST] Running Liquidity Pool Slippage & Impact Cost Estimator Test...\n";

    // Setup 3 Ask levels for Buy order:
    // Level 1: $100.00 (1,000,000), 200 shares
    // Level 2: $100.05 (1,000,500), 300 shares
    // Level 3: $100.10 (1,001,000), 500 shares
    // Total available = 1,000 shares
    luv::BookLevel asks[3];
    asks[0] = {1'000'000, 200};
    asks[1] = {1'000'500, 300};
    asks[2] = {1'001'000, 500};

    // 1. Buy 500 shares:
    // Takes 200 @ $100.00 + 300 @ $100.05
    // Total notional = (200 * 1,000,000) + (300 * 1,000,500) = 200,000,000 + 300,150,000 = 500,150,000
    // VWAP = 500,150,000 / 500 = 1,000,300 ($100.03)
    // Slippage = (100.03 - 100.00) / 100.00 * 10,000 = 3 bps
    auto est1 = luv::SlippageEstimator::estimate_slippage(true, 500, asks, 3);
    assert(est1.fully_filled);
    assert(est1.total_filled_qty == 500);
    assert(est1.levels_consumed == 2);
    assert(est1.vwap_exec_price == 1'000'300);
    assert(est1.price_slippage_bps == 3);

    // 2. Buy 1,200 shares (exceeds available 1,000 shares)
    auto est2 = luv::SlippageEstimator::estimate_slippage(true, 1200, asks, 3);
    assert(!est2.fully_filled);
    assert(est2.total_filled_qty == 1000);
    assert(est2.levels_consumed == 3);

    std::cout << "[TEST] 500 shares VWAP: " << est1.vwap_exec_price
              << " | Slippage BPS: " << est1.price_slippage_bps
              << " | Levels Consumed: " << est1.levels_consumed << "\n";

    std::cout << "[TEST] Liquidity Pool Slippage & Impact Cost Estimator Test Passed!\n";
    return 0;
}
