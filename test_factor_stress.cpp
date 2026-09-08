#include "luv_factor_stress.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_macro_factor_stress_testing() {
    luv::risk::MacroFactorStressEngine engine;

    // Position 1: $1,000,000 Long SPY (Beta = 1.0)
    assert(engine.register_position_factors(luv::risk::AssetFactorSensitivities{
        .symbol_idx = 1,
        .position_notional = 1'000'000'0000LL,
        .equity_beta = 1.0,
        .interest_rate_dv01 = 0.0,
        .commodity_beta = 0.0,
        .fx_delta = 0.0,
        .vega = 0.0
    }));

    // Position 2: $2,000,000 Long 10Y Treasuries (DV01 = -$1,500 per bp rise)
    assert(engine.register_position_factors(luv::risk::AssetFactorSensitivities{
        .symbol_idx = 2,
        .position_notional = 2'000'000'0000LL,
        .equity_beta = 0.0,
        .interest_rate_dv01 = -1500.0,
        .commodity_beta = 0.0,
        .fx_delta = 0.0,
        .vega = 0.0
    }));

    // Macro Stress Scenario: SPX drops -10% (-0.10) AND Rates surge +50 bps (+50.0)
    // SPY Loss: $1,000,000 * 1.0 * -0.10 = -$100,000
    // Rates Loss: -1500 * 50 = -$75,000
    // Total Projected Loss = -$175,000 (-1750000000 scaled)
    luv::risk::MacroStressScenario severe_shock{
        .spx_shock_pct = -0.10,
        .rates_shock_bps = 50.0,
        .oil_shock_pct = 0.0,
        .fx_shock_pct = 0.0,
        .vix_shock_pct = 0.0
    };

    int64_t portfolio_equity = 500'000'0000LL; // $500,000 initial equity
    auto result = engine.evaluate_stress_scenario(severe_shock, portfolio_equity);

    assert(result.projected_pnl == -175'000'0000LL);
    // Drawdown = -175k / 500k = -35.0% -> Exceeds -30%, triggers stress margin call
    assert(std::fabs(result.projected_drawdown_pct - (-35.0)) < 1e-4);
    assert(result.margin_call_triggered == true);

    std::printf("[PASS] test_macro_factor_stress_testing (Projected Loss: -$%lld, Drawdown: %.1f%%, Margin Call: %s)\n",
        static_cast<long long>(-result.projected_pnl / 10000),
        result.projected_drawdown_pct,
        result.margin_call_triggered ? "TRIGGERED" : "NONE");
}

int main() {
    test_macro_factor_stress_testing();
    std::printf("All macro factor stress testing tests passed successfully.\n");
    return 0;
}
