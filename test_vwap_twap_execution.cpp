#include "luv_vwap_twap_execution.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting VWAP / TWAP Execution Performance Scheduler Tests..." << std::endl;

    luv::VWAPTWAPConfig cfg{};
    cfg.num_intervals = 5;
    cfg.max_participation_limit_pct = 15.0;
    cfg.arrival_benchmark_price = 100.00;

    luv::VWAPTWAPExecutionScheduler scheduler(cfg);

    // 1. TWAP Schedule Generation (Uniform Slicing)
    uint64_t parent_order = 10'000;
    auto twap_schedule = scheduler.generate_schedule(luv::ExecutionAlgoType::TWAP, parent_order);

    std::cout << "  TWAP Slices (5 intervals):" << std::endl;
    for (size_t i = 0; i < 5; ++i) {
        std::cout << "    Slice " << i << ": " << twap_schedule[i].slice_target_shares 
                  << " shares, Cumulative: " << twap_schedule[i].cumulative_target_shares 
                  << " (" << twap_schedule[i].target_percentage << "%)" << std::endl;
        assert(twap_schedule[i].slice_target_shares == 2000);
    }
    assert(twap_schedule[4].cumulative_target_shares == 10'000);

    // 2. VWAP Schedule Generation with U-shaped Intraday Volume Curve
    // [30%, 15%, 10%, 15%, 30%]
    double u_curve[5]{0.30, 0.15, 0.10, 0.15, 0.30};
    scheduler.set_volume_profile(u_curve, 5);

    auto vwap_schedule = scheduler.generate_schedule(luv::ExecutionAlgoType::VWAP, parent_order);
    std::cout << "  VWAP Slices (U-shaped Curve):" << std::endl;
    for (size_t i = 0; i < 5; ++i) {
        std::cout << "    Slice " << i << ": " << vwap_schedule[i].slice_target_shares 
                  << " shares, Cumulative: " << vwap_schedule[i].cumulative_target_shares << std::endl;
    }

    assert(vwap_schedule[0].slice_target_shares == 3000);
    assert(vwap_schedule[1].slice_target_shares == 1500);
    assert(vwap_schedule[2].slice_target_shares == 1000);
    assert(vwap_schedule[3].slice_target_shares == 1500);
    assert(vwap_schedule[4].slice_target_shares == 3000);
    assert(vwap_schedule[4].cumulative_target_shares == 10'000);

    // 3. Record Fills & Evaluate Execution Slippage
    // Market executed 200,000 shares total at VWAP = $100.20
    // Algorithm executed 10,000 shares at VWAP = $100.10 (10 bps price improvement / negative slippage)
    scheduler.record_fill(3000, 100.05, 60'000, 100.10);
    scheduler.record_fill(1500, 100.08, 30'000, 100.15);
    scheduler.record_fill(1000, 100.10, 20'000, 100.20);
    scheduler.record_fill(1500, 100.12, 30'000, 100.25);
    scheduler.record_fill(3000, 100.15, 60'000, 100.30);

    auto rep = scheduler.evaluate_performance();
    std::cout << "  Executed VWAP:       $" << rep.executed_vwap << std::endl;
    std::cout << "  Market VWAP:         $" << rep.market_vwap << std::endl;
    std::cout << "  Slippage (bps):      " << rep.slippage_bps << " bps" << std::endl;
    std::cout << "  Participation Rate:  " << rep.participation_rate_pct << "%" << std::endl;
    std::cout << "  Within Limit:        " << (rep.within_participation_limit ? "YES" : "NO") << std::endl;

    assert(rep.total_filled_shares == 10'000);
    assert(rep.executed_vwap > 100.0 && rep.executed_vwap < 100.20);
    assert(rep.participation_rate_pct == 5.0); // 10k / 200k = 5.0% (< 15% limit)
    assert(rep.within_participation_limit);

    std::cout << "[PASS] VWAP / TWAP Execution Performance Scheduler Tests Passed!" << std::endl;
    return 0;
}
