#include <iostream>
#include <cassert>
#include <string_view>
#include "luv_form_13f.hpp"

int main() {
    std::cout << "[TEST] Running SEC Form 13F Reporter Test...\n";

    luv::Form13fReporter reporter;

    // Sub-threshold check
    reporter.add_holding("037833100", "APPLE INC", "COM", 100'000, 22'000'000); // $22M
    assert(!reporter.is_filing_required());
    assert(reporter.get_total_market_value() == 22'000'000);

    // Cross $100M threshold
    reporter.add_holding("594918104", "MICROSOFT CORP", "COM", 200'000, 85'000'000); // $85M -> Total $107M
    assert(reporter.is_filing_required());
    assert(reporter.get_total_market_value() == 107'000'000);
    assert(reporter.get_holding_count() == 2);

    char buffer[4096];
    size_t len = reporter.export_xml_summary(buffer, sizeof(buffer));
    assert(len > 0);
    std::string_view xml(buffer, len);
    assert(xml.find("<filingManagerThresholdMet>true</filingManagerThresholdMet>") != std::string_view::npos);
    assert(xml.find("<totalValueUSD>107000000</totalValueUSD>") != std::string_view::npos);
    assert(xml.find("<cusip>037833100</cusip>") != std::string_view::npos);
    assert(xml.find("<cusip>594918104</cusip>") != std::string_view::npos);

    std::cout << "[TEST] SEC Form 13F Reporter Test Passed!\n";
    return 0;
}
