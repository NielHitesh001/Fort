#include "luv_worm_audit.hpp"
#include <cassert>
#include <cstdio>

void test_sec_17a4_worm_audit_integrity() {
    luv::compliance::WormAuditLogValidator validator;

    // Append 5 sequential audit events
    assert(validator.append_record(1, 1'000'000'000ULL, 1, "NEW ORDER #101 BUY AAPL"));
    assert(validator.append_record(2, 1'000'050'000ULL, 2, "FILL ORDER #101 100 @ 150.00"));
    assert(validator.append_record(3, 1'000'100'000ULL, 3, "MARGIN CHECK PASS"));
    assert(validator.append_record(4, 1'000'150'000ULL, 1, "NEW ORDER #102 SELL MSFT"));
    assert(validator.append_record(5, 1'000'200'000ULL, 2, "FILL ORDER #102 200 @ 300.00"));

    assert(validator.record_count() == 5);
    // Verify cryptographic chain
    assert(validator.verify_chain_integrity() == true);

    // Non-monotonic sequence attempt (must fail WORM rule)
    assert(!validator.append_record(4, 1'000'250'000ULL, 1, "ILLEGAL REWOUND SEQUENCE"));

    std::printf("[PASS] test_sec_17a4_worm_audit_integrity (Validated 5 chained WORM records successfully)\n");
}

int main() {
    test_sec_17a4_worm_audit_integrity();
    std::printf("All SEC Rule 17a-4 WORM audit tests passed successfully.\n");
    return 0;
}
