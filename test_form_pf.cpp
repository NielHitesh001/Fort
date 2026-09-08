#include "luv_form_pf.hpp"
#include <cassert>
#include <cstdio>

void test_sec_form_pf_reporting() {
    luv::compliance::FormPfReporter reporter;

    // Equities: $1.2B Long, $800M Short -> $2.0B GNE
    assert(reporter.record_exposure(1, 1'200'000'000'0000LL, 800'000'000'0000LL));
    // Derivatives: $1.0B Long, $1.0B Short -> $2.0B GNE
    assert(reporter.record_exposure(3, 1'000'000'000'0000LL, 1'000'000'000'0000LL));

    // NAV = $1.6B (Qualifying Large Hedge Fund > $1.5B threshold)
    int64_t nav = 1'600'000'000'0000LL;
    auto report = reporter.generate_report(nav);

    // Total GNE = $4.0B ($40,000,000,000,000 scaled)
    assert(report.gross_notional_exposure == 4'000'000'000'0000LL);
    // Leverage ratio = 4.0B / 1.6B = 2.5x
    assert(report.gross_leverage_ratio == 2.5);
    assert(report.tier == luv::compliance::FormPfTier::kLargeHedgeFund);
    assert(report.requires_quarterly_filing == true);

    std::printf("[PASS] test_sec_form_pf_reporting (GNE: $4.0B, Gross Leverage: 2.5x, Large Hedge Fund Tier)\n");
}

int main() {
    test_sec_form_pf_reporting();
    std::printf("All SEC Form PF reporting tests passed successfully.\n");
    return 0;
}
