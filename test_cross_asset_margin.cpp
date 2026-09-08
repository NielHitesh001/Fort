#include "luv_cross_asset_margin.hpp"
#include <cassert>
#include <cstdio>

void test_cross_asset_portfolio_margin_optimization() {
    luv::risk::CrossAssetMarginOptimizer optimizer;

    // Product 1: SPX E-mini Futures (Standalone Margin: $12,000 = 120000000 scaled, Long Delta +50)
    assert(optimizer.register_product_position(1, 12000'0000LL, 50));

    // Product 2: SPY ETF (Standalone Margin: $10,000 = 100000000 scaled, Short Delta -50)
    assert(optimizer.register_product_position(2, 10000'0000LL, -50));

    // Register 80% correlation offset between SPX Futures and SPY ETF
    assert(optimizer.register_correlation_offset(1, 2, 0.80));

    // Total Standalone Margin = $12,000 + $10,000 = $22,000
    // Offset base = min($12k, $10k) = $10,000
    // Relief = $10,000 * 80% = $8,000
    // Optimized Portfolio Margin = $22,000 - $8,000 = $14,000 (140000000 scaled)
    int64_t opt_margin = optimizer.compute_optimized_portfolio_margin();
    assert(opt_margin == 14000'0000LL);

    std::printf("[PASS] test_cross_asset_portfolio_margin_optimization (Standalone: $22k -> Optimized: $%lld)\n",
        static_cast<long long>(opt_margin / 10000));
}

int main() {
    test_cross_asset_portfolio_margin_optimization();
    std::printf("All cross-asset margin optimization tests passed successfully.\n");
    return 0;
}
