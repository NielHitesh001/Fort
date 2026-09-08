#include "luv_sec_form_pf_leverage.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting SEC Form PF Leverage & Borrowing Metrics Tests..." << std::endl;

    // 1. Qualifying Large Hedge Fund ($1B NAV, $3.5B GNE, $800M Debt)
    luv::FormPFFundData fund1{};
    std::strncpy(fund1.fund_id, "FUND_GLOBAL_MAC", 15);
    fund1.net_asset_value = 1'000'000'000.0;
    fund1.gross_notional_exposure = 3'500'000'000.0; // 3.5x Gross leverage
    fund1.net_notional_exposure = 500'000'000.0;     // 0.5x Net leverage
    fund1.secured_borrowings = 700'000'000.0;
    fund1.unsecured_borrowings = 100'000'000.0;
    fund1.unencumbered_cash = 250'000'000.0;         // 25% cash buffer
    fund1.collateral_pledged = 800'000'000.0;
    fund1.collateral_rehypothecated = 400'000'000.0; // 50% rehypothecated
    fund1.daily_turnover_usd = 50'000'000.0;

    auto m1 = luv::FormPFLeverageEngine::calculate_metrics(fund1);

    std::cout << "  Gross Leverage:        " << m1.gross_leverage_ratio << "x" << std::endl;
    std::cout << "  Borrowing to NAV:      " << m1.borrowing_to_nav_ratio << "x" << std::endl;
    std::cout << "  Unencumbered Cash:     " << m1.unencumbered_cash_ratio * 100.0 << "%" << std::endl;
    std::cout << "  Rehypothecation:       " << m1.rehypothecation_pct << "%" << std::endl;
    std::cout << "  Qualifying Large HF:   " << (m1.qualifying_hedge_fund ? "YES" : "NO") << std::endl;

    assert(m1.qualifying_hedge_fund);
    assert(std::abs(m1.gross_leverage_ratio - 3.5) < 1e-4);
    assert(std::abs(m1.borrowing_to_nav_ratio - 0.8) < 1e-4);
    assert(std::abs(m1.rehypothecation_pct - 50.0) < 1e-4);
    assert(!m1.leverage_alert);

    // 2. High-Risk Fund Breaching Regulatory Leverage Alert (> 5.0x GNE or > 2.0x Debt)
    luv::FormPFFundData fund2 = fund1;
    fund2.gross_notional_exposure = 6'500'000'000.0; // 6.5x GNE
    fund2.secured_borrowings = 2'200'000'000.0;      // 2.2x Debt
    auto m2 = luv::FormPFLeverageEngine::calculate_metrics(fund2);

    assert(m2.leverage_alert);
    assert(m2.gross_leverage_ratio > 5.0);
    assert(m2.borrowing_to_nav_ratio > 2.0);

    std::cout << "[PASS] SEC Form PF Leverage & Borrowing Metrics Tests Passed!" << std::endl;
    return 0;
}
