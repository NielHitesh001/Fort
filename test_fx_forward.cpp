#include "luv_fx_forward.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_fx_forward_curve_and_outright() {
    luv::fx::FxForwardCurveEngine engine(1.0850); // Spot EUR/USD = 1.0850

    // Add swap point pillars (in pips):
    // 1M (0.083Y): +10.0 pips
    // 3M (0.250Y): +30.0 pips
    // 6M (0.500Y): +60.0 pips
    // 1Y (1.000Y): +120.0 pips
    assert(engine.add_forward_pillar(0.083, 10.0));
    assert(engine.add_forward_pillar(0.250, 30.0));
    assert(engine.add_forward_pillar(0.500, 60.0));
    assert(engine.add_forward_pillar(1.000, 120.0));

    // 1Y Outright: Spot 1.0850 + 120 pips (0.0120) = 1.0970
    double fwd_1y = engine.get_forward_outright(1.0);
    assert(std::fabs(fwd_1y - 1.0970) < 1e-6);

    // Broken date: 9M (0.750Y) tenor interpolation between 6M (+60 pips) and 1Y (+120 pips) -> +90.0 pips
    // 9M Outright: 1.0850 + 0.0090 = 1.0940
    double fwd_9m = engine.get_forward_outright(0.75);
    assert(std::fabs(fwd_9m - 1.0940) < 1e-6);

    std::printf("[PASS] test_fx_forward_curve_and_outright (1Y Outright: %.4f, 9M Outright: %.4f)\n",
        fwd_1y, fwd_9m);
}

int main() {
    test_fx_forward_curve_and_outright();
    std::printf("All FX forward curve tests passed successfully.\n");
    return 0;
}
