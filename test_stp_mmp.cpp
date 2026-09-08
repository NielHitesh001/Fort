#include "luv_stp.hpp"
#include <cassert>
#include <cstdio>

void test_stp_policies() {
    uint32_t firm_a = 100;
    uint32_t firm_b = 200;

    // Different firms -> No STP trigger
    auto res_diff = luv::SelfTradePreventionEngine::evaluate(
        firm_a, firm_b, luv::StpMode::kCancelNewest, 100, 100);
    assert(!res_diff.self_trade_detected);
    assert(res_diff.adjusted_incoming_qty == 100);
    assert(res_diff.adjusted_resting_qty == 100);

    // 1. Cancel Newest (CN)
    auto res_cn = luv::SelfTradePreventionEngine::evaluate(
        firm_a, firm_a, luv::StpMode::kCancelNewest, 100, 100);
    assert(res_cn.self_trade_detected);
    assert(res_cn.cancel_incoming);
    assert(!res_cn.cancel_resting);
    assert(res_cn.adjusted_incoming_qty == 0);
    assert(res_cn.adjusted_resting_qty == 100);

    // 2. Cancel Oldest (CO)
    auto res_co = luv::SelfTradePreventionEngine::evaluate(
        firm_a, firm_a, luv::StpMode::kCancelOldest, 100, 100);
    assert(res_co.self_trade_detected);
    assert(!res_co.cancel_incoming);
    assert(res_co.cancel_resting);
    assert(res_co.adjusted_incoming_qty == 100);
    assert(res_co.adjusted_resting_qty == 0);

    // 3. Cancel Both (CB)
    auto res_cb = luv::SelfTradePreventionEngine::evaluate(
        firm_a, firm_a, luv::StpMode::kCancelBoth, 100, 100);
    assert(res_cb.self_trade_detected);
    assert(res_cb.cancel_incoming);
    assert(res_cb.cancel_resting);

    // 4. Decrement and Cancel (DC) - Aggressor larger
    auto res_dc_agg_large = luv::SelfTradePreventionEngine::evaluate(
        firm_a, firm_a, luv::StpMode::kDecrementAndCancel, 150, 100);
    assert(res_dc_agg_large.self_trade_detected);
    assert(!res_dc_agg_large.cancel_incoming);
    assert(res_dc_agg_large.cancel_resting);
    assert(res_dc_agg_large.adjusted_incoming_qty == 50);
    assert(res_dc_agg_large.adjusted_resting_qty == 0);

    // 5. Decrement and Cancel (DC) - Passive larger
    auto res_dc_pass_large = luv::SelfTradePreventionEngine::evaluate(
        firm_a, firm_a, luv::StpMode::kDecrementAndCancel, 60, 100);
    assert(res_dc_pass_large.self_trade_detected);
    assert(res_dc_pass_large.cancel_incoming);
    assert(!res_dc_pass_large.cancel_resting);
    assert(res_dc_pass_large.adjusted_incoming_qty == 0);
    assert(res_dc_pass_large.adjusted_resting_qty == 40);

    std::printf("[PASS] test_stp_policies\n");
}

void test_mmp_burst_protection() {
    luv::MmpConfig config;
    config.window_ns = 1'000'000'000; // 1s
    config.max_traded_volume = 1000;
    config.max_delta_volume = 600;

    luv::MarketMakerProtection mmp(config);
    assert(!mmp.is_tripped());

    uint64_t ts = 10'000'000;
    // Trade 1: Buy 300
    assert(mmp.on_trade(ts, luv::exec::kBuy, 300, 10000));
    assert(!mmp.is_tripped());

    // Trade 2: Buy 350 -> Delta = 650 > 600 max delta -> Trips MMP
    ts += 100'000'000; // +100ms
    bool ok = mmp.on_trade(ts, luv::exec::kBuy, 350, 10000);
    assert(!ok);
    assert(mmp.is_tripped());
    assert(mmp.trip_ts_ns() == ts);

    // Further trades rejected while tripped
    assert(!mmp.on_trade(ts + 10, luv::exec::kBuy, 10, 10000));

    // Reset clears trip state
    mmp.reset();
    assert(!mmp.is_tripped());

    std::printf("[PASS] test_mmp_burst_protection\n");
}

int main() {
    test_stp_policies();
    test_mmp_burst_protection();
    std::printf("All Self-Trade Prevention & MMP tests passed successfully.\n");
    return 0;
}
