#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include <algorithm>

namespace luv {
namespace compliance {

struct WormAuditRecord {
    uint64_t sequence_id = 0;
    uint64_t timestamp_ns = 0;
    uint32_t record_type = 0; // 1: Order, 2: Fill, 3: Risk Breaches, 4: Margin
    uint64_t previous_hash = 0;
    uint64_t current_hash = 0;
    char payload_summary[64]{};
};

class WormAuditLogValidator {
public:
    static constexpr size_t kMaxRecords = 512;
    static constexpr uint64_t kRetentionPeriodNs = 6ULL * 365ULL * 24ULL * 3600ULL * 1'000'000'000ULL; // 6 years (SEC 17a-4 standard)

    WormAuditLogValidator() noexcept : num_records_(0), last_hash_(0xCBF29CE484222325ULL) {} // FNV offset basis

    bool append_record(uint64_t seq, uint64_t timestamp_ns, uint32_t rec_type, const char* summary) noexcept {
        if (num_records_ >= kMaxRecords) return false;

        // Monotonic sequence verification
        if (num_records_ > 0 && seq <= records_[num_records_ - 1].sequence_id) {
            return false; // WORM violation: Sequence must strictly increase
        }

        WormAuditRecord rec{};
        rec.sequence_id = seq;
        rec.timestamp_ns = timestamp_ns;
        rec.record_type = rec_type;
        rec.previous_hash = last_hash_;

        if (summary) {
            std::strncpy(rec.payload_summary, summary, sizeof(rec.payload_summary) - 1);
        }

        // FNV-1a Hash computation over previous_hash + payload
        uint64_t hash = rec.previous_hash ^ seq;
        hash *= 0x100000001B3ULL;
        hash ^= timestamp_ns;
        hash *= 0x100000001B3ULL;
        hash ^= rec_type;
        hash *= 0x100000001B3ULL;

        for (size_t i = 0; i < sizeof(rec.payload_summary) && rec.payload_summary[i] != '\0'; ++i) {
            hash ^= static_cast<uint8_t>(rec.payload_summary[i]);
            hash *= 0x100000001B3ULL;
        }

        rec.current_hash = hash;
        last_hash_ = hash;

        records_[num_records_++] = rec;
        return true;
    }

    // Validates entire WORM cryptographic chain for tamper evidence
    bool verify_chain_integrity() const noexcept {
        if (num_records_ == 0) return true;

        uint64_t expected_prev = 0xCBF29CE484222325ULL;

        for (size_t i = 0; i < num_records_; ++i) {
            const auto& rec = records_[i];
            if (rec.previous_hash != expected_prev) {
                return false; // Broken link / Tampered record
            }

            uint64_t hash = rec.previous_hash ^ rec.sequence_id;
            hash *= 0x100000001B3ULL;
            hash ^= rec.timestamp_ns;
            hash *= 0x100000001B3ULL;
            hash ^= rec.record_type;
            hash *= 0x100000001B3ULL;

            for (size_t j = 0; j < sizeof(rec.payload_summary) && rec.payload_summary[j] != '\0'; ++j) {
                hash ^= static_cast<uint8_t>(rec.payload_summary[j]);
                hash *= 0x100000001B3ULL;
            }

            if (rec.current_hash != hash) {
                return false; // Modified payload / Invalid hash
            }

            expected_prev = hash;
        }

        return true;
    }

    size_t record_count() const noexcept { return num_records_; }

private:
    std::array<WormAuditRecord, kMaxRecords> records_{};
    size_t num_records_{0};
    uint64_t last_hash_{0xCBF29CE484222325ULL};
};

} // namespace compliance
} // namespace luv
