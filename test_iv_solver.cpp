#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_iv_solver.hpp"

int main() {
    std::cout << "[TEST] Running Implied Volatility Solver Test...\n";

    double spot = 100.0;
    double strike = 100.0;
    double rate = 0.05;
    double expiry = 0.5; // 6 months
    double target_vol = 0.25; // 25% vol

    // 1. Calculate Call price with target_vol
    double call_price = luv::IvSolver::bs_price(spot, strike, rate, expiry, target_vol, luv::OptionType::Call);
    assert(call_price > 0.0);

    // 2. Solve for IV
    luv::IvSolverParams params;
    params.spot = spot;
    params.strike = strike;
    params.rate = rate;
    params.time_to_expiry = expiry;
    params.type = luv::OptionType::Call;
    params.market_price = call_price;

    double solved_vol = luv::IvSolver::solve(params);
    assert(std::abs(solved_vol - target_vol) < 1e-4);

    // 3. Put test
    double put_price = luv::IvSolver::bs_price(spot, strike, rate, expiry, target_vol, luv::OptionType::Put);
    params.type = luv::OptionType::Put;
    params.market_price = put_price;

    double solved_put_vol = luv::IvSolver::solve(params);
    assert(std::abs(solved_put_vol - target_vol) < 1e-4);

    std::cout << "[TEST] Solved Call Vol: " << solved_vol << " (target: " << target_vol << ")\n";
    std::cout << "[TEST] Solved Put Vol: " << solved_put_vol << " (target: " << target_vol << ")\n";
    std::cout << "[TEST] Implied Volatility Solver Test Passed!\n";
    return 0;
}
