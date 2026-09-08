#include "luv_cftc_swap_reporting.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting CFTC Part 43 / Part 45 Swap Reporting Tests..." << std::endl;

    luv::CFTCSwapReportingEngine engine;

    // 1. Generate USI/UTI
    char usi_buf[52]{0};
    const char* reporting_lei = "5493006MHB84DD0ZWV18";
    assert(luv::CFTCSwapReportingEngine::generate_usi_uti(reporting_lei, 10042, usi_buf, sizeof(usi_buf)));
    std::cout << "  Generated USI/UTI: " << usi_buf << std::endl;
    assert(std::strlen(usi_buf) == 42);
    assert(std::strncmp(usi_buf, reporting_lei, 20) == 0);

    // 2. Submit Standard SEF Cleared Interest Rate Swap ($50M notional)
    luv::SwapTradeRecord trade1{};
    trade1.trade_id = 1001;
    trade1.asset_class = luv::CFTCSwapAssetClass::InterestRate;
    trade1.venue_type = luv::CFTCExecutionVenueType::SEF;
    std::strncpy(trade1.reporting_counterparty_lei, reporting_lei, 20);
    std::strncpy(trade1.usi_uti, usi_buf, 42);
    trade1.notional_amount = 50'000'000.0;
    std::strncpy(trade1.notional_currency, "USD", 3);
    trade1.fixed_rate_or_price = 0.0425; // 4.25% fixed
    trade1.execution_timestamp_ns = 1'000'000'000ULL;
    trade1.is_block_trade = false;
    trade1.is_cleared = true;

    assert(engine.submit_swap_trade(trade1));
    assert(engine.trade_count() == 1);
    assert(engine.disseminated_count() == 1);

    const auto* diss1 = engine.get_dissemination_record(0);
    assert(diss1 != nullptr);
    assert(diss1->capped_rounded_notional == 50'000'000.0);
    assert(diss1->public_release_time_ns == trade1.execution_timestamp_ns); // Immediate real-time

    // 3. Submit Large Block Trade ($500M notional -> Capped at $250M with 15-min delay)
    char usi_block[52]{0};
    assert(luv::CFTCSwapReportingEngine::generate_usi_uti(reporting_lei, 10043, usi_block, sizeof(usi_block)));

    luv::SwapTradeRecord trade2{};
    trade2.trade_id = 1002;
    trade2.asset_class = luv::CFTCSwapAssetClass::InterestRate;
    trade2.venue_type = luv::CFTCExecutionVenueType::SEF;
    std::strncpy(trade2.reporting_counterparty_lei, reporting_lei, 20);
    std::strncpy(trade2.usi_uti, usi_block, 42);
    trade2.notional_amount = 500'000'000.0; // $500M
    trade2.is_block_trade = true;
    trade2.execution_timestamp_ns = 2'000'000'000ULL;

    assert(engine.submit_swap_trade(trade2));
    const auto* diss2 = engine.get_dissemination_record(1);
    assert(diss2 != nullptr);
    std::cout << "  Block Trade Capped Notional: $" << diss2->capped_rounded_notional << std::endl;
    assert(diss2->capped_rounded_notional == 250'000'000.0); // Capped at $250M
    assert(diss2->public_release_time_ns == (trade2.execution_timestamp_ns + (15ULL * 60 * 1'000'000'000ULL)));

    std::cout << "[PASS] CFTC Part 43 / Part 45 Swap Reporting Tests Passed!" << std::endl;
    return 0;
}
