#include "luv_amm_liquidity.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_amm_constant_product_swap() {
    // 1M USDC, 1M USDT pool, 0.30% fee tier
    luv::crypto::AmmLiquidityEngine pool(1'000'000.0, 1'000'000.0, 0.0030);

    assert(std::fabs(pool.get_spot_price() - 1.0) < 1e-6);

    // Swap 10,000 USDC for USDT:
    // Net input = 10,000 * (1 - 0.003) = 9,970 USDC
    // Amount out = (1,000,000 * 9,970) / (1,000,000 + 9,970) = 9,871.58 USDT
    auto swap_res = pool.swap_token0_for_token1(10'000.0);

    assert(swap_res.amount_in == 10000.0);
    assert(swap_res.amount_out > 9850.0 && swap_res.amount_out < 9900.0);
    assert(swap_res.fee_paid == 30.0);
    assert(swap_res.price_impact_bps > 50.0 && swap_res.price_impact_bps < 150.0);

    // Reserves updated: Reserve0 = 1,010,000, Reserve1 < 1,000,000
    assert(pool.get_reserve0() == 1'010'000.0);
    assert(pool.get_reserve1() < 1'000'000.0);

    std::printf("[PASS] test_amm_constant_product_swap (Swap 10k -> Out: %.2f, Fee: $%.2f, Impact: %.2f bps)\n",
        swap_res.amount_out, swap_res.fee_paid, swap_res.price_impact_bps);
}

int main() {
    test_amm_constant_product_swap();
    std::printf("All AMM liquidity tests passed successfully.\n");
    return 0;
}
