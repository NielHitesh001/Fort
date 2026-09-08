#include "luv_algo.hpp"
#include <cassert>
#include <cstdio>

void test_twap_slicer() {
    int64_t total_qty = 1000;
    uint32_t total_intervals = 10;
    int64_t cum_filled = 0;

    for (uint32_t i = 0; i < total_intervals; ++i) {
        int64_t slice = luv::algo::TwapSlicer::compute_next_slice(total_qty, cum_filled, i, total_intervals);
        assert(slice == 100);
        cum_filled += slice;
    }
    assert(cum_filled == total_qty);

    // Completed
    assert(luv::algo::TwapSlicer::compute_next_slice(total_qty, cum_filled, 10, total_intervals) == 0);

    std::printf("[PASS] test_twap_slicer\n");
}

void test_vwap_slicer() {
    int64_t total_qty = 10000;
    // Bucket 0 (Open surge): 14% -> 1400 shares
    int64_t open_slice = luv::algo::VwapSlicer::compute_bucket_target(total_qty, 0);
    assert(open_slice == 1400);

    // Bucket 5 (Lunch lull): 5% -> 500 shares
    int64_t lunch_slice = luv::algo::VwapSlicer::compute_bucket_target(total_qty, 5);
    assert(lunch_slice == 500);

    // Bucket 12 (Close surge): 16% -> 1600 shares
    int64_t close_slice = luv::algo::VwapSlicer::compute_bucket_target(total_qty, 12);
    assert(close_slice == 1600);

    std::printf("[PASS] test_vwap_slicer\n");
}

void test_pov_slicer() {
    int64_t remaining = 500;
    // Market traded 1000 shares, target participation = 15% -> child order = 150 shares
    int64_t child1 = luv::algo::PovSlicer::compute_child_qty(remaining, 1000, 15.0);
    assert(child1 == 150);

    // Market traded 5000 shares, target participation = 20% -> would be 1000, capped at remaining 500!
    int64_t child2 = luv::algo::PovSlicer::compute_child_qty(remaining, 5000, 20.0);
    assert(child2 == 500);

    std::printf("[PASS] test_pov_slicer\n");
}

int main() {
    test_twap_slicer();
    test_vwap_slicer();
    test_pov_slicer();
    std::printf("All algorithmic execution tests passed successfully.\n");
    return 0;
}
