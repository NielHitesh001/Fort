#include "luv_rebates.hpp"
#include <cassert>
#include <cstdio>

void test_fee_rebate_optimization() {
    luv::routing::FeeRebateOptimizer optimizer;

    // Venue 1 (Nasdaq): 30 bps taker fee + 1 bps routing = 31 bps
    optimizer.register_venue_fee(1, "NASDAQ", 20.0, 30.0, 1.0);
    // Venue 2 (BATS - Inverted/Low fee): 10 bps taker fee + 1 bps routing = 11 bps
    optimizer.register_venue_fee(2, "BATS", 25.0, 10.0, 1.0);

    int64_t quoted_ask = 10000; // $1.0000

    // Compute net buy price on Nasdaq:
    // Net price = 10000 + round(10000 * 0.0031) = 10000 + 31 = 10031
    auto net_nasdaq = optimizer.compute_taker_net_price(1, luv::exec::kBuy, quoted_ask);
    assert(net_nasdaq.effective_net_price == 10031);

    // Compute net buy price on BATS:
    // Net price = 10000 + round(10000 * 0.0011) = 10000 + 11 = 10011 -> Cheaper!
    auto net_bats = optimizer.compute_taker_net_price(2, luv::exec::kBuy, quoted_ask);
    assert(net_bats.effective_net_price == 10011);
    assert(net_bats.effective_net_price < net_nasdaq.effective_net_price);

    std::printf("[PASS] test_fee_rebate_optimization (Nasdaq Net: %lld, BATS Net: %lld)\n",
        static_cast<long long>(net_nasdaq.effective_net_price),
        static_cast<long long>(net_bats.effective_net_price));
}

int main() {
    test_fee_rebate_optimization();
    std::printf("All fee and rebate optimization tests passed successfully.\n");
    return 0;
}
