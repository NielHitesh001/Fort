#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <cstring>

namespace luv {

enum class RecordRetentionCategory : uint8_t {
    PermanentLifetime = 0,    // SEA Rule 17a-4(a): Corporate charter, minute books, stock certificates
    SixYearRetention = 1,     // SEA Rule 17a-4(c): Blotters, general ledgers, customer ledgers, stock records
    ThreeYearRetention = 2,   // SEA Rule 17a-4(b): Order tickets, trade confirms, internal communications
    TwoYearImmediateAccess = 3 // First 2 years required to be immediately accessible
};

enum class StorageComplianceMode : uint8_t {
    WORM_Classic = 0,         // Non-rewriteable, non-erasable (optical/hardware WORM)
    AuditTrailAlternative = 1  // SEC 2022 modernized verifiable complete audit trail format
};

struct RecordMetadata {
    uint64_t record_id{0};
    RecordRetentionCategory category{RecordRetentionCategory::ThreeYearRetention};
    uint64_t creation_timestamp_ns{0};
    uint64_t payload_hash{0};         // 64-bit fast hash / SHA digest
    uint64_t previous_record_hash{0}; // Cryptographic linkage
    uint32_t payload_size_bytes{0};
    bool is_litigation_hold{false};   // Litigation hold overrides standard purge schedules
    bool is_worm_sealed{false};       // Sealed against modification or deletion
};

struct Rule17a4VerificationResult {
    size_t total_records_checked{0};
    size_t compliant_records_count{0};
    size_t litigation_hold_count{0};
    size_t expired_records_eligible_for_purge{0};
    bool hash_chain_integrity_valid{true};
    bool third_party_undertaking_active{true};
    bool is_fully_compliant{true};
};

class SecRule17a4RecordEngine {
public:
    static constexpr size_t kLedgerCapacity = 256;
    static constexpr uint64_t kOneYearNs = 365ULL * 24ULL * 3600ULL * 1'000'000'000ULL;
    static constexpr uint64_t kTwoYearsNs = 2ULL * kOneYearNs;
    static constexpr uint64_t kThreeYearsNs = 3ULL * kOneYearNs;
    static constexpr uint64_t kSixYearsNs = 6ULL * kOneYearNs;

    SecRule17a4RecordEngine() noexcept : head_(0), count_(0), third_party_undertaking_on_file_(true) {}

    void set_third_party_undertaking(bool on_file) noexcept {
        third_party_undertaking_on_file_ = on_file;
    }

    bool append_record(uint64_t record_id, RecordRetentionCategory cat, uint64_t creation_ns, 
                       uint64_t payload_hash, uint32_t payload_size, bool litigation_hold = false) noexcept 
    {
        if (count_ >= kLedgerCapacity) return false;

        uint64_t prev_hash = 0;
        if (count_ > 0) {
            size_t last_idx = (head_ == 0) ? (kLedgerCapacity - 1) : (head_ - 1);
            prev_hash = ledger_[last_idx].payload_hash;
        }

        ledger_[head_] = RecordMetadata{
            record_id,
            cat,
            creation_ns,
            payload_hash,
            prev_hash,
            payload_size,
            litigation_hold,
            true /* is_worm_sealed */
        };

        head_ = (head_ + 1) % kLedgerCapacity;
        ++count_;
        return true;
    }

    bool set_litigation_hold(uint64_t record_id, bool hold_active) noexcept {
        for (size_t i = 0; i < count_; ++i) {
            if (ledger_[i].record_id == record_id) {
                ledger_[i].is_litigation_hold = hold_active;
                return true;
            }
        }
        return false;
    }

    // ✅ FIXED: Removed unused parameter 'mode' - it was defined but never used in the function
    Rule17a4VerificationResult audit_records(uint64_t current_timestamp_ns) const noexcept 
    {
        Rule17a4VerificationResult result{};
        result.total_records_checked = count_;
        result.third_party_undertaking_active = third_party_undertaking_on_file_;

        if (count_ == 0) {
            result.is_fully_compliant = result.third_party_undertaking_active;
            return result;
        }

        uint64_t expected_prev_hash = 0;

        for (size_t i = 0; i < count_; ++i) {
            const auto& rec = ledger_[i];

            // 1. Verify WORM / Audit trail seal
            if (!rec.is_worm_sealed) {
                result.hash_chain_integrity_valid = false;
            }

            // 2. Verify blockchain linkage
            if (i > 0 && rec.previous_record_hash != expected_prev_hash) {
                result.hash_chain_integrity_valid = false;
            }
            expected_prev_hash = rec.payload_hash;

            // 3. Check litigation hold
            if (rec.is_litigation_hold) {
                ++result.litigation_hold_count;
            }

            // 4. Check retention duration and purge eligibility
            uint64_t age_ns = (current_timestamp_ns >= rec.creation_timestamp_ns) ? 
                              (current_timestamp_ns - rec.creation_timestamp_ns) : 0;
            
            bool expired = false;
            switch (rec.category) {
                case RecordRetentionCategory::ThreeYearRetention:
                    if (age_ns >= kThreeYearsNs && !rec.is_litigation_hold) expired = true;
                    break;
                case RecordRetentionCategory::SixYearRetention:
                    if (age_ns >= kSixYearsNs && !rec.is_litigation_hold) expired = true;
                    break;
                case RecordRetentionCategory::TwoYearImmediateAccess:
                    if (age_ns >= kTwoYearsNs && !rec.is_litigation_hold) expired = true;
                    break;
                case RecordRetentionCategory::PermanentLifetime:
                    expired = false; // Permanent records cannot be purged
                    break;
            }

            if (expired) {
                ++result.expired_records_eligible_for_purge;
            }

            if (rec.is_worm_sealed) {
                ++result.compliant_records_count;
            }
        }

        result.is_fully_compliant = (result.compliant_records_count == result.total_records_checked) &&
                                    result.hash_chain_integrity_valid &&
                                    result.third_party_undertaking_active;
        return result;
    }

private:
    std::array<RecordMetadata, kLedgerCapacity> ledger_{};
    size_t head_{0};
    size_t count_{0};
    bool third_party_undertaking_on_file_{true};
};

} // namespace luv