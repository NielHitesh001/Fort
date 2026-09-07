#include <cassert>
#include <cstdio>
#include <cstring>
#include "luv_risk_engine.hpp"

using namespace luv;

void test_pre_trade_risk_checks() {
    std::printf("[test_pre_trade_risk_checks] Running...\n");
    PreTradeRiskConfig cfg{};
    cfg.max_single_order_notional = 100'000 * 10'000ULL; // $100k
    cfg.max_single_order_quantity = 1'000;
    cfg.price_collar_bps = 500; // 5%
    cfg.max_gross_notional = 1'000'000 * 10'000ULL; // $1M
    cfg.max_net_position_per_symbol = 2'000;
    cfg.max_portfolio_drawdown = 50'000 * 10'000ULL; // $50k

    AutonomousRiskEngine<16> engine(cfg);

    // 1. Normal order within limits
    assert(engine.validate_pre_trade(0, 0, 100 * 10'000, 500, 100 * 10'000, 1000) ==
           RiskValidationResult::kApproved);

    // 2. Quantity exceeding single limit (> 1,000)
    assert(engine.validate_pre_trade(0, 0, 100 * 10'000, 1'500, 100 * 10'000, 1000) ==
           RiskValidationResult::kRejected_ExceedsSingleQuantity);

    // 3. Notional exceeding single limit (> $100k)
    assert(engine.validate_pre_trade(0, 0, 200 * 10'000, 800, 200 * 10'000, 1000) ==
           RiskValidationResult::kRejected_ExceedsSingleNotional);

    // 4. Fat-finger price collar (> 5% deviation from ref price $100)
    assert(engine.validate_pre_trade(0, 0, 110 * 10'000, 100, 100 * 10'000, 1000) ==
           RiskValidationResult::kRejected_PriceCollarViolation);

    // 5. Symbol Halting
    engine.halt_symbol(0);
    assert(engine.is_symbol_halted(0));
    assert(engine.validate_pre_trade(0, 0, 100 * 10'000, 100, 100 * 10'000, 1000) ==
           RiskValidationResult::kRejected_SymbolHalted);
    engine.resume_symbol(0);
    assert(!engine.is_symbol_halted(0));

    // 6. Net Position Limit per symbol (> 2,000)
    engine.record_fill(0, 0, 100 * 10'000, 1'800, 0, 1000);
    assert(engine.net_position(0) == 1'800);
    // Proposed buy of 500 would push net position to 2,300 (> 2,000)
    assert(engine.validate_pre_trade(0, 0, 100 * 10'000, 500, 100 * 10'000, 1000) ==
           RiskValidationResult::kRejected_ExceedsNetPosition);

    // 7. Max Drawdown Hard Stop -> Kill Switch
    assert(!engine.is_kill_switch_active());
    // Record losing fill with -$60,000 loss (exceeding $50k max drawdown)
    engine.record_fill(0, 1, 100 * 10'000, 500, -60'000 * 10'000LL, 2000);
    assert(engine.is_kill_switch_active());
    assert(std::strstr(engine.kill_switch_reason(), "drawdown") != nullptr);

    // After kill switch is tripped, all pre-trade orders are rejected
    assert(engine.validate_pre_trade(1, 0, 50 * 10'000, 10, 50 * 10'000, 3000) ==
           RiskValidationResult::kRejected_KillSwitchTripped);

    // Reset kill switch
    engine.reset_kill_switch();
    assert(!engine.is_kill_switch_active());
    assert(engine.validate_pre_trade(1, 0, 50 * 10'000, 10, 50 * 10'000, 4000) ==
           RiskValidationResult::kApproved);

    std::printf("[test_pre_trade_risk_checks] PASSED\n");
}

int main() {
    test_pre_trade_risk_checks();
    std::printf("ALL RISK ENGINE TESTS PASSED\n");
    return 0;
}
