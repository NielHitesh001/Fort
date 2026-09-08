#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_kyles_lambda.hpp"

int main() {
    std::cout << "[TEST] Running Kyle's Lambda Price Impact Estimator Test...\n";

    luv::KylesLambdaEstimator estimator;

    // Simulate linear relationship: Delta P = 0.05 * Q + noise
    // 100 shares -> +5 ticks, 200 shares -> +10 ticks, -100 shares -> -5 ticks, -200 shares -> -10 ticks
    estimator.add_observation(100, 5);
    estimator.add_observation(200, 10);
    estimator.add_observation(300, 15);
    estimator.add_observation(-100, -5);
    estimator.add_observation(-200, -10);
    estimator.add_observation(-300, -15);

    double lambda = estimator.get_lambda();
    double r_squared = estimator.get_permanent_impact_ratio();

    // Lambda should be precisely 0.05
    assert(std::abs(lambda - 0.05) < 1e-4);
    // Perfect linear correlation -> R^2 should be 1.0
    assert(std::abs(r_squared - 1.0) < 1e-4);

    std::cout << "[TEST] Estimated Kyle's Lambda: " << lambda
              << " | Hasbrouck Permanent Impact Ratio (R^2): " << r_squared << "\n";

    std::cout << "[TEST] Kyle's Lambda Price Impact Estimator Test Passed!\n";
    return 0;
}
