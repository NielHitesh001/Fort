#include "luv_order_flow_imbalance.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting Multi-Level Order Flow Imbalance (OFI) Tests..." << std::endl;

    luv::OrderFlowImbalanceEngine ofi_engine;

    // Snapshot 0: Base initial book state
    luv::MultiLevelBookSnapshot snap0{};
    snap0.timestamp_ns = 1'000'000;
    snap0.num_levels = 3;
    // Bids
    snap0.bids[0] = {100.00, 500};
    snap0.bids[1] = {99.99, 1000};
    snap0.bids[2] = {99.98, 1500};
    // Asks
    snap0.asks[0] = {100.01, 500};
    snap0.asks[1] = {100.02, 1000};
    snap0.asks[2] = {100.03, 1500};

    auto res0 = ofi_engine.update(snap0);
    assert(res0.level1_ofi == 0.0);
    assert(res0.multi_level_ofi == 0.0);

    // Snapshot 1: Aggressive bid addition at L1 (500 -> 1200) -> +700 bid delta
    luv::MultiLevelBookSnapshot snap1 = snap0;
    snap1.timestamp_ns = 2'000'000;
    snap1.bids[0] = {100.00, 1200}; // +700 at level 1

    auto res1 = ofi_engine.update(snap1);
    std::cout << "  Event 1 L1 OFI: " << res1.level1_ofi << ", Multi-Level OFI: " << res1.multi_level_ofi << std::endl;
    assert(res1.level1_ofi == 700.0);
    assert(res1.multi_level_ofi == 350.0); // 0.50 * 700
    assert(res1.predicted_price_delta > 0.0);

    // Snapshot 2: Price increase at L1 (Best Bid steps up from 100.00 to 100.01 with 800 size)
    luv::MultiLevelBookSnapshot snap2 = snap1;
    snap2.timestamp_ns = 3'000'000;
    snap2.bids[0] = {100.01, 800}; // New higher best bid: +800
    // Asks step up: L1 ask was 100.01, now consumed/shifted to 100.02
    snap2.asks[0] = {100.02, 600}; // Ask price higher -> -prev_a (-500) -> delta_ask = -500

    auto res2 = ofi_engine.update(snap2);
    std::cout << "  Event 2 (Price step-up) L1 OFI: " << res2.level1_ofi << ", Multi-Level OFI: " << res2.multi_level_ofi << std::endl;
    // delta_bid = +800 (since curr.price > prev.price)
    // delta_ask = -500 (since curr.price > prev.price -> ask consumed/cancelled)
    // ofi_1 = delta_bid - delta_ask = 800 - (-500) = 1300
    assert(res2.level1_ofi == 1300.0);
    assert(res2.multi_level_ofi > 0.0);

    // Snapshot 3: Massive ask wall / dump causing toxic imbalance
    luv::MultiLevelBookSnapshot snap3 = snap2;
    snap3.timestamp_ns = 4'000'000;
    // Level 1 bid drops back to 100.00 with 100 shares (depletion: -prev_b = -800)
    snap3.bids[0] = {100.00, 100};
    // Level 1 ask aggressive dump: 100.01 with 10,000 shares (lower ask price -> delta_ask = +10000)
    snap3.asks[0] = {100.01, 10000};

    auto res3 = ofi_engine.update(snap3);
    std::cout << "  Event 3 (Massive Ask Dump) Multi-Level OFI: " << res3.multi_level_ofi 
              << ", Toxicity: " << (res3.toxic_imbalance_detected ? "TRUE" : "FALSE") << std::endl;
    // OFI_1 = (-800) - (10000) = -10800
    assert(res3.level1_ofi == -10800.0);
    assert(res3.multi_level_ofi < -2500.0);
    assert(res3.toxic_imbalance_detected);

    std::cout << "[PASS] Multi-Level Order Flow Imbalance (OFI) Tests Passed!" << std::endl;
    return 0;
}
