#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_variance_swap.hpp"

int main() {
    std::cout << "[TEST] Running Cross-Asset Variance Swap Valuation Test...\n";

    luv::VarianceSwapTerms terms;
    terms.strike_variance = 0.04; // 20% vol strike (0.20^2 = 0.04)
    terms.notional_vega = 50'000.0; // $50k per vol point
    terms.time_to_maturity_years = 1.0;
    terms.total_expected_observations = 252;

    luv::VarianceSwapEngine engine(terms);

    // Feed price series with 20% annualized daily volatility:
    // dt = 1/252, daily sigma = 0.20 / sqrt(252) ~ 0.0126
    double spot = 100.0;
    engine.add_price_observation(spot);

    for (int i = 1; i <= 50; ++i) {
        double ret = (i % 2 == 0) ? 0.0126 : -0.0126;
        spot *= (1.0 + ret);
        engine.add_price_observation(spot);
    }

    double realized_var = engine.compute_realized_variance();
    double realized_vol = engine.compute_realized_volatility();
    double payoff = engine.compute_settlement_pnl();

    assert(realized_vol > 0.18 && realized_vol < 0.22); // Realized vol close to 20%
    assert(std::abs(realized_var - 0.04) < 0.01);
    assert(std::abs(payoff) < 10000.0); // PnL close to 0 because realized vol matched strike

    std::cout << "[TEST] Realized Variance: " << realized_var
              << " | Realized Vol: " << (realized_vol * 100.0) << "%"
              << " | Settlement PnL: $" << payoff << "\n";

    std::cout << "[TEST] Cross-Asset Variance Swap Valuation Test Passed!\n";
    return 0;
}
