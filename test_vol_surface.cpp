#include "luv_vol_surface.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_volatility_surface_interpolation() {
    luv::derivatives::VolatilitySurfaceEngine vol_surf;

    // Add points for SPY options smile
    // 1M (0.083Y): 90% Strike (0.28 vol), ATM 100% (0.18 vol), 110% (0.16 vol)
    assert(vol_surf.add_vol_point(0.083, 900000, 0.28));
    assert(vol_surf.add_vol_point(0.083, 1000000, 0.18));
    assert(vol_surf.add_vol_point(0.083, 1100000, 0.16));

    // 1Y (1.0Y): 90% Strike (0.24 vol), ATM 100% (0.20 vol), 110% (0.19 vol)
    assert(vol_surf.add_vol_point(1.000, 900000, 0.24));
    assert(vol_surf.add_vol_point(1.000, 1000000, 0.20));
    assert(vol_surf.add_vol_point(1.000, 1100000, 0.19));

    // Exact query ATM 1Y
    double vol_1y_atm = vol_surf.get_implied_vol(1.000, 1000000);
    assert(std::fabs(vol_1y_atm - 0.20) < 1e-4);

    // Interpolated query 6M (0.5Y), ATM (1000000) -> should be between 0.18 and 0.20
    double vol_6m_atm = vol_surf.get_implied_vol(0.500, 1000000);
    assert(vol_6m_atm >= 0.17 && vol_6m_atm <= 0.21);

    std::printf("[PASS] test_volatility_surface_interpolation (1Y ATM: %.2f%%, 6M ATM: %.2f%%)\n",
        vol_1y_atm * 100.0, vol_6m_atm * 100.0);
}

int main() {
    test_volatility_surface_interpolation();
    std::printf("All volatility surface tests passed successfully.\n");
    return 0;
}
