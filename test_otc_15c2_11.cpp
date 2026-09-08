#include <iostream>
#include <cassert>
#include "luv_otc_15c2_11.hpp"

int main() {
    std::cout << "[TEST] Running SEC Rule 15c2-11 OTC Quotation Validator Test...\n";

    // 1. Current issuer with fresh filing (30 days old) -> Quotation allowed
    luv::OtcSecurityState s1;
    s1.symbol_id = 1;
    s1.filing_status = luv::IssuerFilingStatus::Current;
    s1.days_since_last_filing = 30;
    s1.consecutive_days_unquoted = 0;
    s1.is_piggyback_eligible = true;
    assert(luv::Otc15c211Validator::is_quotation_permitted(s1));

    // 2. Delinquent issuer -> Quotation prohibited
    luv::OtcSecurityState s2;
    s2.symbol_id = 2;
    s2.filing_status = luv::IssuerFilingStatus::Delinquent;
    s2.days_since_last_filing = 200;
    assert(!luv::Otc15c211Validator::is_quotation_permitted(s2));

    // 3. Delinquent issuer with Unsolicited Customer Order exemption -> Allowed
    s2.is_unsolicited_customer_order = true;
    assert(luv::Otc15c211Validator::is_quotation_permitted(s2));

    // 4. Piggyback exception lapsed (>4 days unquoted) -> Prohibited
    luv::OtcSecurityState s3;
    s3.symbol_id = 3;
    s3.filing_status = luv::IssuerFilingStatus::Current;
    s3.days_since_last_filing = 60;
    s3.consecutive_days_unquoted = 5; // >4 days gap
    s3.is_piggyback_eligible = true;
    assert(!luv::Otc15c211Validator::is_quotation_permitted(s3));

    // 5. Exempt ADR -> Allowed
    luv::OtcSecurityState s4;
    s4.symbol_id = 4;
    s4.filing_status = luv::IssuerFilingStatus::ExemptADR;
    assert(luv::Otc15c211Validator::is_quotation_permitted(s4));

    std::cout << "[TEST] SEC Rule 15c2-11 OTC Quotation Validator Test Passed!\n";
    return 0;
}
