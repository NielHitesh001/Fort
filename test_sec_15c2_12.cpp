#include "luv_sec_15c2_12.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[TEST] Starting SEC Rule 15c2-12 Municipal Disclosure Validator Tests..." << std::endl;

    luv::SEC15c212Validator validator;

    // 1. Offering 1: Compliant Primary Offering ($50M General Obligation Bond)
    luv::MuniOfferingRecord o1{};
    std::strncpy(o1.cusip_prefix, "123456", 6);
    std::strncpy(o1.issuer_name, "State of California GO Bonds 2026", 34);
    o1.aggregate_principal = 50'000'000.0;
    o1.min_denomination = 5000;
    o1.maturity_days = 3650;
    o1.exemption = luv::MuniExemptionStatus::NonExempt;
    o1.pos_deemed_final = true;
    o1.final_os_received = true;
    o1.pos_timestamp_ns = 1'000'000'000ULL;
    o1.final_os_timestamp_ns = o1.pos_timestamp_ns + (3ULL * 24 * 3600 * 1'000'000'000ULL); // 3 days (within 7 days)
    o1.cdsa_executed = true;
    o1.active_event_flags = 0;
    o1.emma_filing_verified = true;
    assert(validator.register_offering(o1));

    auto res1 = validator.validate_trade("123456", 2'000'000'000ULL, 100'000.0);
    assert(res1.trade_allowed);
    assert(!res1.disclosure_deficiency);
    assert(!res1.material_event_warning);

    // 2. Offering 2: Small Offering Exemption (< $1,000,000)
    luv::MuniOfferingRecord o2{};
    std::strncpy(o2.cusip_prefix, "234567", 6);
    o2.aggregate_principal = 750'000.0; // Under $1M
    o2.exemption = luv::MuniExemptionStatus::SmallOfferingExempt;
    o2.pos_deemed_final = false; // Exempt
    o2.cdsa_executed = false;    // Exempt
    assert(validator.register_offering(o2));

    auto res2 = validator.validate_trade("234567", 2'000'000'000ULL, 50'000.0);
    assert(res2.trade_allowed);
    assert(res2.exempt);

    // 3. Offering 3: Disclosure Deficiency (POS not deemed final)
    luv::MuniOfferingRecord o3{};
    std::strncpy(o3.cusip_prefix, "345678", 6);
    o3.aggregate_principal = 25'000'000.0;
    o3.exemption = luv::MuniExemptionStatus::NonExempt;
    o3.pos_deemed_final = false; // Violation!
    o3.cdsa_executed = true;
    assert(validator.register_offering(o3));

    auto res3 = validator.validate_trade("345678", 2'000'000'000ULL, 100'000.0);
    assert(!res3.trade_allowed);
    assert(res3.disclosure_deficiency);
    assert(std::strcmp(res3.violation_reason, "POS_NOT_DEEMED_FINAL") == 0);

    // 4. Offering 4: Severe Material Event Default (Bankruptcy / Payment Delinquency unverified on EMMA)
    luv::MuniOfferingRecord o4{};
    std::strncpy(o4.cusip_prefix, "456789", 6);
    o4.aggregate_principal = 10'000'000.0;
    o4.pos_deemed_final = true;
    o4.cdsa_executed = true;
    o4.active_event_flags = static_cast<uint16_t>(luv::MuniMaterialEventType::PrincipalAndInterestPaymentDelinquency);
    o4.emma_filing_verified = false; // Not resolved/filed
    assert(validator.register_offering(o4));

    auto res4 = validator.validate_trade("456789", 2'000'000'000ULL, 50'000.0);
    assert(!res4.trade_allowed);
    assert(res4.material_event_warning);
    assert(std::strcmp(res4.violation_reason, "UNRESOLVED_SEVERE_MATERIAL_DEFAULT") == 0);

    // 5. Offering 5: Unregistered Muni
    auto res5 = validator.validate_trade("999999", 2'000'000'000ULL, 50'000.0);
    assert(!res5.trade_allowed);
    assert(std::strcmp(res5.violation_reason, "MUNI_ISSUER_NOT_REGISTERED") == 0);

    std::cout << "[PASS] SEC Rule 15c2-12 Municipal Disclosure Validator Tests Passed!" << std::endl;
    return 0;
}
