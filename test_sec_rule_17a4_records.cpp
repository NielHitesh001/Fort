#include "luv_sec_rule_17a4_records.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_worm_append_and_hash_chain_verification() {
    SecRule17a4RecordEngine engine;
    uint64_t base_time_ns = 1'000'000'000'000ULL;

    // Append 3-year record (order ticket)
    assert(engine.append_record(101, RecordRetentionCategory::ThreeYearRetention, base_time_ns, 0xABC1, 256));
    // Append 6-year record (general ledger)
    assert(engine.append_record(102, RecordRetentionCategory::SixYearRetention, base_time_ns + 1000, 0xDEF2, 1024));
    // Append permanent record (charter)
    assert(engine.append_record(103, RecordRetentionCategory::PermanentLifetime, base_time_ns + 2000, 0x9993, 2048));

    auto res = engine.audit_records(base_time_ns + 3000);
    assert(res.total_records_checked == 3);
    assert(res.compliant_records_count == 3);
    assert(res.hash_chain_integrity_valid == true);
    assert(res.is_fully_compliant == true);
    assert(res.expired_records_eligible_for_purge == 0);
}

void test_retention_purge_and_litigation_hold() {
    SecRule17a4RecordEngine engine;
    uint64_t base_time_ns = 1'000'000'000'000ULL;

    // Record A: 3-year ticket created 4 years ago (eligible for purge)
    engine.append_record(201, RecordRetentionCategory::ThreeYearRetention, base_time_ns, 0x1111, 128);

    // Record B: 3-year ticket created 4 years ago under Litigation Hold (CANNOT purge)
    engine.append_record(202, RecordRetentionCategory::ThreeYearRetention, base_time_ns, 0x2222, 128, true /* litigation hold */);

    // Record C: 6-year ledger created 4 years ago (not yet expired)
    engine.append_record(203, RecordRetentionCategory::SixYearRetention, base_time_ns, 0x3333, 512);

    uint64_t four_years_later_ns = base_time_ns + 4ULL * SecRule17a4RecordEngine::kOneYearNs;
    auto res = engine.audit_records(four_years_later_ns);

    assert(res.total_records_checked == 3);
    assert(res.litigation_hold_count == 1);
    assert(res.expired_records_eligible_for_purge == 1); // Only Record A is eligible
    assert(res.is_fully_compliant == true);
}

void test_third_party_access_undertaking_requirement() {
    SecRule17a4RecordEngine engine;
    engine.set_third_party_undertaking(false); // Missing Rule 17a-4(f)(3)(vii) undertaking

    engine.append_record(301, RecordRetentionCategory::ThreeYearRetention, 1'000'000'000ULL, 0x5555, 64);
    auto res = engine.audit_records(2'000'000'000ULL);

    assert(res.third_party_undertaking_active == false);
    assert(res.is_fully_compliant == false);
}

int main() {
    test_worm_append_and_hash_chain_verification();
    test_retention_purge_and_litigation_hold();
    test_third_party_access_undertaking_requirement();
    std::cout << "SEC Rule 17a-4 Electronic Records & WORM Engine tests passed.\n";
    return 0;
}
