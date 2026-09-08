#include <iostream>
#include <cassert>
#include "luv_cftc_rule_575.hpp"

int main() {
    std::cout << "[TEST] Running CFTC Part 38 / CME Rule 575 Anti-Disruptive Trading Validator Test...\n";

    luv::CftcRule575Validator validator;

    // 1. Normal benign activity
    uint64_t t = 1'000'000'000ULL;
    validator.on_order_entry(t, 101, true, 1'000'000, 100);
    validator.on_order_fill(t + 50'000'000, 101, 100); // filled 50ms later
    auto alert1 = validator.evaluate_activity(1, t + 100'000'000);
    assert(alert1.pattern == luv::DisruptivePattern::None);

    // 2. Spoofing pattern: submit 5 large orders and cancel them within 1ms
    for (uint64_t i = 1; i <= 5; ++i) {
        t += 10'000'000;
        validator.on_order_entry(t, 200 + i, true, 1'000'000, 5000);
        validator.on_order_cancel(t + 500'000, 200 + i); // cancelled 0.5ms later (< 5ms)
    }

    auto alert2 = validator.evaluate_activity(1, t + 1'000'000);
    assert(alert2.pattern == luv::DisruptivePattern::Spoofing);
    assert(alert2.severity_score >= 90);

    // 3. Quote stuffing pattern: 25 rapid message bursts within 0.5ms
    luv::CftcRule575Validator qs_validator;
    t = 2'000'000'000ULL;
    for (uint64_t i = 1; i <= 25; ++i) {
        qs_validator.on_order_entry(t + i * 10'000, 300 + i, true, 1'000'000, 10);
    }
    auto alert3 = qs_validator.evaluate_activity(2, t + 300'000);
    assert(alert3.pattern == luv::DisruptivePattern::QuoteStuffing);
    assert(alert3.severity_score == 95);

    std::cout << "[TEST] Detected Spoofing (Severity " << alert2.severity_score << ") and Quote Stuffing (Severity " << alert3.severity_score << ").\n";
    std::cout << "[TEST] CFTC Part 38 / CME Rule 575 Anti-Disruptive Trading Validator Test Passed!\n";
    return 0;
}
