#include "luv_sec_rule_144.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting SEC Rule 144 Restricted Securities Resale Tests..." << std::endl;

    uint64_t one_year_ns = 365ULL * 24 * 3600 * 1'000'000'000ULL;
    uint64_t eight_months_ns = 240ULL * 24 * 3600 * 1'000'000'000ULL;
    uint64_t four_months_ns = 120ULL * 24 * 3600 * 1'000'000'000ULL;

    uint64_t t0 = 1'000'000'000ULL;

    // 1. Holding Period Failure (< 6 months for SEC reporting issuer)
    luv::Rule144SecurityHolding h1{};
    std::strncpy(h1.symbol, "TECH1", 5);
    std::strncpy(h1.shareholder_id, "AFFILIATE_CEO", 13);
    h1.affiliate_status = luv::SellerAffiliateStatus::Affiliate;
    h1.issuer_status = luv::IssuerReportingStatus::SECReportingIssuer;
    h1.acquisition_timestamp_ns = t0;
    h1.total_restricted_shares = 100'000;
    h1.total_shares_outstanding = 10'000'000; // 1% = 100,000
    h1.four_week_avg_weekly_volume = 50'000;  // Ceiling = max(100k, 50k) = 100k
    h1.issuer_filings_current = true;
    h1.form_144_filed = true;

    auto res1 = luv::SECRule144Engine::validate_sale(h1, 10'000, 25.0, t0 + four_months_ns);
    std::cout << "  Test 1 (<6 months) Allowed: " << (res1.sale_permitted ? "YES" : "NO") 
              << ", Reason: " << res1.violation_reason << std::endl;
    assert(!res1.sale_permitted);
    assert(res1.holding_period_breached);

    // 2. Affiliate Compliant Sale (> 6 months holding, within volume limit, Form 144 filed)
    auto res2 = luv::SECRule144Engine::validate_sale(h1, 50'000, 25.0, t0 + eight_months_ns);
    std::cout << "  Test 2 (Compliant Affiliate) Allowed: " << (res2.sale_permitted ? "YES" : "NO") 
              << ", Max Permitted: " << res2.max_permitted_shares << std::endl;
    assert(res2.sale_permitted);
    assert(res2.max_permitted_shares == 100'000);
    assert(res2.form_144_required);

    // 3. Volume Limit Breach (Proposed 150,000 shares when ceiling is 100,000)
    auto res3 = luv::SECRule144Engine::validate_sale(h1, 150'000, 25.0, t0 + eight_months_ns);
    std::cout << "  Test 3 (Volume Breach) Allowed: " << (res3.sale_permitted ? "YES" : "NO") 
              << ", Reason: " << res3.violation_reason << std::endl;
    assert(!res3.sale_permitted);
    assert(res3.volume_limit_breached);
    assert(res3.max_permitted_shares == 100'000);

    // 4. Form 144 Unfiled Failure for Affiliate (> 5,000 shares or > $50,000)
    luv::Rule144SecurityHolding h2 = h1;
    h2.form_144_filed = false; // Not filed!
    auto res4 = luv::SECRule144Engine::validate_sale(h2, 10'000, 25.0, t0 + eight_months_ns);
    assert(!res4.sale_permitted);
    assert(std::strcmp(res4.violation_reason, "FORM_144_FILING_REQUIRED") == 0);

    // 5. Non-Affiliate (> 1 year holding) Unrestricted Free Resale
    luv::Rule144SecurityHolding h3 = h1;
    h3.affiliate_status = luv::SellerAffiliateStatus::NonAffiliate;
    auto res5 = luv::SECRule144Engine::validate_sale(h3, 100'000, 25.0, t0 + one_year_ns + four_months_ns);
    assert(res5.sale_permitted);
    assert(!res5.form_144_required);

    std::cout << "[PASS] SEC Rule 144 Restricted Securities Resale Tests Passed!" << std::endl;
    return 0;
}
