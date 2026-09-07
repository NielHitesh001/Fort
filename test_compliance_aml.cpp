#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "luv_compliance.hpp"

using namespace luv;

void test_kyc_validation() {
    std::printf("[test_kyc_validation] Running...\n");
    ComplianceRegistry<64> registry;

    registry.register_trader(1, KycTier::kTier1_Basic);
    registry.register_trader(2, KycTier::kTier2_Verified);
    registry.register_trader(3, KycTier::kTier3_Institutional);
    registry.register_trader(4, KycTier::kUnverified);
    registry.register_trader(5, KycTier::kTier3_Institutional, /*sanctioned=*/true);

    SarRecord sar{};
    uint64_t now_ns = 1'000'000'000ULL;

    // Unverified trader (Tier 0) must be rejected
    assert(!registry.validate_order(4, 1'000 * 10'000ULL, now_ns, &sar));
    assert(sar.activity_type == SuspiciousActivityType::kKycLimitBreach);

    // Sanctioned trader must be blocked
    assert(!registry.validate_order(5, 1'000 * 10'000ULL, now_ns, &sar));
    assert(sar.activity_type == SuspiciousActivityType::kSanctionsViolation);

    // Tier 1 ($5k single limit, $25k daily)
    assert(registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar)); // Approved
    assert(!registry.validate_order(1, 6'000 * 10'000ULL, now_ns, &sar)); // Exceeds single limit ($5k)
    assert(sar.activity_type == SuspiciousActivityType::kKycLimitBreach);

    // Tier 1 daily volume accumulation
    assert(registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar)); // total 8k
    assert(registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar)); // total 12k
    assert(registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar)); // total 16k
    assert(registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar)); // total 20k
    assert(registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar)); // total 24k
    // Exceeds 25k daily limit:
    assert(!registry.validate_order(1, 4'000 * 10'000ULL, now_ns, &sar));

    std::printf("[test_kyc_validation] PASSED\n");
}

void test_structuring_and_wash_trade_monitoring() {
    std::printf("[test_structuring_and_wash_trade_monitoring] Running...\n");
    TransactionMonitoringEngine<64> monitor;
    SarRecord sar{};
    uint64_t now_ns = 100'000'000ULL;

    // Normal transactions ($2,000) do not trigger structuring
    assert(!monitor.check_structuring(10, 2'000 * 10'000ULL, now_ns, &sar));

    // Transactions in structuring zone ($9,500 just under $10k CTR threshold)
    assert(!monitor.check_structuring(10, 9'500 * 10'000ULL, now_ns, &sar)); // 1st
    assert(!monitor.check_structuring(10, 9'200 * 10'000ULL, now_ns + 1000, &sar)); // 2nd
    // 3rd transaction triggers Structuring SAR
    assert(monitor.check_structuring(10, 9'800 * 10'000ULL, now_ns + 2000, &sar));
    assert(sar.activity_type == SuspiciousActivityType::kStructuring);
    assert(sar.occurrences == 3);
    assert(sar.suspect_trader_id == 10);

    // Wash trading self-cross check
    assert(monitor.check_wash_trade(15, 15, 100, 5000, now_ns + 3000, &sar));
    assert(sar.activity_type == SuspiciousActivityType::kWashTrading);
    assert(sar.suspect_trader_id == 15);

    // Different counterparties do not trigger wash trading
    assert(!monitor.check_wash_trade(15, 16, 100, 5000, now_ns + 4000, &sar));

    // SAR JSON Export verification
    char json_buf[1024]{};
    assert(FinCenSarExporter::to_json(sar, json_buf, sizeof(json_buf)));
    assert(std::strstr(json_buf, "\"regulatory_filing\": \"FinCEN_SAR_FORM_111\"") != nullptr);
    assert(std::strstr(json_buf, "\"WASH_TRADING_SELF_CROSS\"") != nullptr);

    std::printf("[test_structuring_and_wash_trade_monitoring] PASSED\n");
}

void test_compliance_audit_ledger() {
    std::printf("[test_compliance_audit_ledger] Running...\n");
    const char* log_file = "/tmp/test_compliance_audit.log";
    ::unlink(log_file);

    {
        ComplianceAuditLedger ledger;
        assert(ledger.open(log_file));

        for (uint64_t i = 0; i < 25; ++i) {
            assert(ledger.log_event(1'000'000'000ULL + i, static_cast<uint16_t>(i % 5), 1, 0, 50'000ULL * (i + 1)));
        }
        assert(ledger.sequence() == 25);
    }

    // Verify cryptographic integrity
    uint64_t verified = 0;
    uint8_t root_hash[32]{};
    assert(ComplianceAuditLedger::verify_integrity(log_file, verified, root_hash));
    assert(verified == 25);

    ::unlink(log_file);
    std::printf("[test_compliance_audit_ledger] PASSED\n");
}

int main() {
    test_kyc_validation();
    test_structuring_and_wash_trade_monitoring();
    test_compliance_audit_ledger();
    std::printf("ALL COMPLIANCE & AML TESTS PASSED\n");
    return 0;
}
