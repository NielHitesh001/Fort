#include <iostream>
#include <cassert>
#include "luv_regime_switching_mm.hpp"

int main() {
    std::cout << "[TEST] Running Regime-Switching MM Quoter Test...\n";

    luv::RegimeParams params;
    luv::RegimeSwitchingQuoter quoter(params);

    int64_t mid = 1'000'000; // $100.00
    int64_t base_half_spread = 500; // $0.05

    // 1. Normal Regime
    auto q_norm = quoter.compute_adaptive_quote(mid, base_half_spread, 0);
    assert(q_norm.current_regime == luv::MarketRegime::Normal);
    assert(q_norm.spread == 1000); // $0.10
    assert(q_norm.max_allowed_inventory == 500);

    // 2. High Volatility shocks trigger Stressed regime
    for (int i = 0; i < 20; ++i) {
        quoter.on_price_update(1'050'000, 1'000'000); // 5% jumps
    }
    assert(quoter.get_regime() == luv::MarketRegime::Stressed);
    auto q_stress = quoter.compute_adaptive_quote(mid, base_half_spread, 0);
    assert(q_stress.spread == 2000); // 2.0x multiplier = $0.20 spread
    assert(q_stress.max_allowed_inventory == 200);

    // 3. Crisis Regime
    quoter.force_regime(luv::MarketRegime::Crisis);
    auto q_crisis = quoter.compute_adaptive_quote(mid, base_half_spread, 0);
    assert(q_crisis.spread == 5000); // 5.0x multiplier = $0.50 spread
    assert(q_crisis.max_allowed_inventory == 50);

    // 4. Calm Regime
    quoter.force_regime(luv::MarketRegime::Calm);
    auto q_calm = quoter.compute_adaptive_quote(mid, base_half_spread, 0);
    assert(q_calm.spread == 800); // 0.8x multiplier = $0.08 spread
    assert(q_calm.max_allowed_inventory == 1000);

    std::cout << "[TEST] Calm Spread: " << q_calm.spread
              << " | Normal Spread: " << q_norm.spread
              << " | Stressed Spread: " << q_stress.spread
              << " | Crisis Spread: " << q_crisis.spread << "\n";

    std::cout << "[TEST] Regime-Switching MM Quoter Test Passed!\n";
    return 0;
}
