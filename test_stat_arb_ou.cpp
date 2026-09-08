#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_stat_arb_ou.hpp"

int main() {
    std::cout << "[TEST] Running Statistical Arbitrage Ornstein-Uhlenbeck Engine Test...\n";

    luv::StatArbOuEngine engine(2.0, 0.25);

    // Simulate mean-reverting series around mu=10.0 with theta=0.5
    double current = 10.0;
    for (int i = 0; i < 40; ++i) {
        // OU update: dX = 0.5 * (10.0 - X) + noise
        double noise = ((i % 5) - 2) * 0.2;
        current += 0.5 * (10.0 - current) + noise;
        engine.update_spread(current);
    }

    const auto& p1 = engine.get_params();
    assert(p1.theta > 0.0);
    assert(std::abs(p1.mu - 10.0) < 1.0); // Estimated mu near 10.0
    assert(p1.sigma_ou > 0.0);

    // Inject massive positive outlier (+4 std deviations)
    engine.update_spread(10.0 + 4.0 * p1.sigma_ou);
    const auto& p2 = engine.get_params();
    assert(p2.current_zscore > 1.8);
    assert(p2.signal == luv::StatArbSignal::ShortSpread);

    // Inject massive negative outlier (-4 std deviations)
    engine.update_spread(10.0 - 4.0 * p1.sigma_ou);
    const auto& p3 = engine.get_params();
    assert(p3.current_zscore < -1.8);
    assert(p3.signal == luv::StatArbSignal::LongSpread);

    std::cout << "[TEST] Estimated Theta: " << p1.theta
              << " | Mu: " << p1.mu
              << " | Sigma OU: " << p1.sigma_ou
              << " | Z-Score (outlier): " << p2.current_zscore << "\n";

    std::cout << "[TEST] Statistical Arbitrage Ornstein-Uhlenbeck Engine Test Passed!\n";
    return 0;
}
