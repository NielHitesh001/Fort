#include "luv_tick_rule.hpp"
#include <cassert>
#include <cstdio>

void test_sec_rule_612() {
    const char* err = nullptr;

    // Prices >= $1.00 (10000 in 4 decimals)
    assert(luv::rules::SecRule612Validator::validate_tick(15000, &err)); // $1.50 -> Valid
    assert(luv::rules::SecRule612Validator::validate_tick(10000, &err)); // $1.00 -> Valid
    assert(luv::rules::SecRule612Validator::validate_tick(10100, &err)); // $1.01 -> Valid
    assert(!luv::rules::SecRule612Validator::validate_tick(10050, &err)); // $1.0050 -> Sub-penny violation on >= $1.00!

    // Prices < $1.00
    assert(luv::rules::SecRule612Validator::validate_tick(5000, &err)); // $0.50 -> Valid
    assert(luv::rules::SecRule612Validator::validate_tick(5001, &err)); // $0.5001 -> Valid (sub-penny allowed below $1)
    assert(luv::rules::SecRule612Validator::validate_tick(1, &err));    // $0.0001 -> Valid

    std::printf("[PASS] test_sec_rule_612\n");
}

void test_mifid_dynamic_tick_regime() {
    const char* err = nullptr;

    // Price = 1.5000 (15000) -> Band: 10000 - 20000 -> Min tick = 5 ($0.0005)
    assert(luv::rules::MifidDynamicTickValidator::validate_mifid_tick(15005, &err));
    assert(!luv::rules::MifidDynamicTickValidator::validate_mifid_tick(15002, &err)); // Not multiple of 5

    // Price = 60.0000 (600000) -> Band: 500000+ -> Min tick = 200 ($0.0200)
    assert(luv::rules::MifidDynamicTickValidator::validate_mifid_tick(600200, &err));
    assert(!luv::rules::MifidDynamicTickValidator::validate_mifid_tick(600100, &err)); // Not multiple of 200

    std::printf("[PASS] test_mifid_dynamic_tick_regime\n");
}

int main() {
    test_sec_rule_612();
    test_mifid_dynamic_tick_regime();
    std::printf("All SEC Rule 612 & MiFID II tick size tests passed successfully.\n");
    return 0;
}
