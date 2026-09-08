#include "luv_var.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

void test_parametric_var() {
    double notional = 1'000'000.0; // $1M portfolio
    double vol = 0.20;             // 20% annual vol

    auto var = luv::risk::PortfolioVarEngine::compute_parametric_var(notional, vol, 1.0);

    assert(var.var_95_notional > 0.0);
    assert(var.var_99_notional > var.var_95_notional);
    assert(var.expected_shortfall > var.var_99_notional);

    // 1-day 95% VaR on $1M at 20% vol ~ $20,723
    assert(var.var_95_notional > 15000.0 && var.var_95_notional < 25000.0);

    std::printf("[PASS] test_parametric_var (95%% VaR: $%.2f, 99%% VaR: $%.2f, ES: $%.2f)\n",
        var.var_95_notional, var.var_99_notional, var.expected_shortfall);
}

void test_historical_var_and_stress() {
    double notional = 1'000'000.0;
    std::array<double, 100> returns{};

    // Synthetic distribution from -0.05 to +0.05
    for (size_t i = 0; i < 100; ++i) {
        returns[i] = -0.05 + static_cast<double>(i) * 0.001;
    }

    auto h_var = luv::risk::PortfolioVarEngine::compute_historical_var<100>(notional, returns);
    assert(h_var.var_95_notional > 0.0);
    assert(h_var.var_99_notional >= h_var.var_95_notional);

    // Stress test: 10% market crash
    double pnl = luv::risk::PortfolioVarEngine::stress_test_pnl(notional, -10.0);
    assert(pnl == -100000.0); // -$100k loss

    std::printf("[PASS] test_historical_var_and_stress\n");
}

int main() {
    test_parametric_var();
    test_historical_var_and_stress();
    std::printf("All portfolio VaR & stress testing tests passed successfully.\n");
    return 0;
}
