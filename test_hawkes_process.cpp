#include <iostream>
#include <cassert>
#include <cmath>
#include "luv_hawkes_process.hpp"

int main() {
    std::cout << "[TEST] Running Hawkes Process Flow Toxicity & Jump Intensity Test...\n";

    luv::HawkesParams params;
    params.base_intensity_mu = 1.0;
    params.alpha = 3.0;
    params.beta = 6.0;
    params.high_intensity_threshold = 10.0;

    luv::HawkesProcessEstimator estimator(params);
    assert(estimator.get_branching_ratio() == 0.5); // Stationary (0.5 < 1.0)

    // 1. Single isolated event at t=1.0s
    estimator.on_event(1.0);
    assert(estimator.get_current_intensity() == (1.0 + 3.0)); // 4.0
    assert(!estimator.is_toxic_cluster_active(1.0));

    // After 2 seconds (t=3.0s), intensity decays back near baseline mu=1.0
    double decayed_int = estimator.get_intensity_at(3.0);
    assert(decayed_int < 1.1);

    // 2. Toxic rapid burst of 8 events arriving in quick succession (every 0.05s)
    double t = 4.0;
    for (int i = 0; i < 8; ++i) {
        t += 0.05;
        estimator.on_event(t);
    }

    double burst_int = estimator.get_current_intensity();
    assert(burst_int > params.high_intensity_threshold); // > 10.0
    assert(estimator.is_toxic_cluster_active(t));

    std::cout << "[TEST] Base Intensity: 1.0 | Decayed: " << decayed_int
              << " | Burst Jump Intensity: " << burst_int
              << " | Toxic Alert: " << (estimator.is_toxic_cluster_active(t) ? "TRUE" : "FALSE") << "\n";

    std::cout << "[TEST] Hawkes Process Flow Toxicity & Jump Intensity Test Passed!\n";
    return 0;
}
