#include "luv_isda_simm.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting ISDA SIMM v2.6 Margin Calculator Tests..." << std::endl;

    luv::ISDASIMMEngine simm;

    // 1. Add Interest Rate sensitivities across 1Y, 5Y, 10Y, 30Y tenors
    // Net USD portfolio PV01
    luv::IRSensitivityEntry ir1{};
    std::strncpy(ir1.currency, "USD", 3);
    ir1.tenor = luv::IRTenor::Tenor_1Y;
    ir1.delta_pv01 = 15000.0; // $15,000 / bp
    assert(simm.add_ir_sensitivity(ir1));

    luv::IRSensitivityEntry ir2{};
    std::strncpy(ir2.currency, "USD", 3);
    ir2.tenor = luv::IRTenor::Tenor_5Y;
    ir2.delta_pv01 = -10000.0; // -$10,000 / bp
    assert(simm.add_ir_sensitivity(ir2));

    luv::IRSensitivityEntry ir3{};
    std::strncpy(ir3.currency, "USD", 3);
    ir3.tenor = luv::IRTenor::Tenor_10Y;
    ir3.delta_pv01 = 25000.0; // $25,000 / bp
    assert(simm.add_ir_sensitivity(ir3));

    // 2. Add FX Sensitivities
    luv::FXSensitivityEntry fx1{};
    std::strncpy(fx1.ccy_pair, "EURUSD", 6);
    fx1.ccy_category = 1; // High liquidity
    fx1.delta_sensitivity = 5'000'000.0; // $5M delta
    assert(simm.add_fx_sensitivity(fx1));

    luv::FXSensitivityEntry fx2{};
    std::strncpy(fx2.ccy_pair, "USDJPY", 6);
    fx2.ccy_category = 1;
    fx2.delta_sensitivity = -3'000'000.0; // -$3M delta
    assert(simm.add_fx_sensitivity(fx2));

    // 3. Add Equity Sensitivities
    luv::EquitySensitivityEntry eq1{};
    std::strncpy(eq1.ticker, "AAPL", 4);
    eq1.bucket_id = 1; // Tech Large Cap Dev
    eq1.delta_sensitivity = 2'000'000.0; // $2M delta
    assert(simm.add_equity_sensitivity(eq1));

    luv::EquitySensitivityEntry eq2{};
    std::strncpy(eq2.ticker, "MSFT", 4);
    eq2.bucket_id = 1;
    eq2.delta_sensitivity = 1'500'000.0;
    assert(simm.add_equity_sensitivity(eq2));

    double ir_margin = simm.calculate_ir_delta_margin();
    double fx_margin = simm.calculate_fx_margin();
    double eq_margin = simm.calculate_equity_margin();
    auto total_breakdown = simm.calculate_total_margin();

    std::cout << "  IR Delta Initial Margin: $" << ir_margin << std::endl;
    std::cout << "  FX Initial Margin:       $" << fx_margin << std::endl;
    std::cout << "  Equity Initial Margin:   $" << eq_margin << std::endl;
    std::cout << "  Total ISDA SIMM Margin:  $" << total_breakdown.total_initial_margin << std::endl;

    assert(ir_margin > 500'000.0);
    assert(fx_margin > 200'000.0);
    assert(eq_margin > 400'000.0);
    assert(total_breakdown.total_initial_margin > ir_margin);
    assert(total_breakdown.total_initial_margin > fx_margin);
    assert(total_breakdown.total_initial_margin < (ir_margin + fx_margin + eq_margin)); // Diversification benefit

    std::cout << "[PASS] ISDA SIMM v2.6 Margin Calculator Tests Passed!" << std::endl;
    return 0;
}
