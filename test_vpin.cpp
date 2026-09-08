#include "luv_vpin.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_vpin_order_flow_toxicity() {
    // 1,000 shares per volume bucket
    luv::microstructure::VpinCalculator vpin(1'000);

    // Scenario 1: Symmetrical flow (500 Buy, 500 Sell per bucket) -> Low VPIN toxicity (~0.0)
    for (int i = 0; i < 10; ++i) {
        vpin.add_trade(luv::exec::kBuy, 500);
        vpin.add_trade(luv::exec::kSell, 500);
    }
    double vpin_balanced = vpin.compute_vpin();
    assert(vpin_balanced < 0.10);

    // Scenario 2: Severe one-sided toxic flow (100% aggressive buy orders for 10 buckets)
    for (int i = 0; i < 10; ++i) {
        vpin.add_trade(luv::exec::kBuy, 1000);
    }
    double vpin_toxic = vpin.compute_vpin();
    // VPIN toxicity surges
    assert(vpin_toxic > 0.40);

    std::printf("[PASS] test_vpin_order_flow_toxicity (Balanced VPIN: %.3f, Toxic Flow VPIN: %.3f)\n",
        vpin_balanced, vpin_toxic);
}

int main() {
    test_vpin_order_flow_toxicity();
    std::printf("All VPIN order flow toxicity tests passed successfully.\n");
    return 0;
}
