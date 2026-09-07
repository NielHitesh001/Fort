#pragma once

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

namespace luv {

enum class KycTier : uint8_t {
    kUnverified = 0,
    kTier1_Basic = 1,
    kTier2_Verified = 2,
    kTier3_Institutional = 3,
};

struct KycLimits {
    uint64_t max_single_notional = 0;
    uint64_t max_daily_notional = 0;
};

[[nodiscard]] constexpr KycLimits get_kyc_limits(KycTier tier) noexcept {
    switch (tier) {
        case KycTier::kUnverified:
            return {0, 0};
        case KycTier::kTier1_Basic:
            return {5'000 * 10'000ULL, 25'000 * 10'000ULL}; // $5k single, $25k daily
        case KycTier::kTier2_Verified:
            return {100'000 * 10'000ULL, 1'000'000 * 10'000ULL}; // $100k single, $1M daily
        case KycTier::kTier3_Institutional:
            return {100'000'000 * 10'000ULL, 1'000'000'000 * 10'000ULL}; // $100M single, $1B daily
    }
    return {0, 0};
}

struct CustomerProfile {
    uint16_t trader_id = 0;
    KycTier tier = KycTier::kUnverified;
    bool ofac_sanctioned = false;
    bool suspended = false;
    uint64_t daily_notional_traded = 0;
    uint64_t last_day_reset_ns = 0;
};

enum class SuspiciousActivityType : uint8_t {
    kNone = 0,
    kStructuring,
    kWashTrading,
    kLayeringSpoofing,
    kVelocityAnomaly,
    kSanctionsViolation,
    kKycLimitBreach,
};

struct SarRecord {
    uint64_t sar_id = 0;
    uint64_t timestamp_ns = 0;
    uint16_t suspect_trader_id = 0;
    SuspiciousActivityType activity_type = SuspiciousActivityType::kNone;
    uint64_t suspicious_amount = 0;
    uint32_t occurrences = 0;
    char narrative[256]{};
};

class FinCenSarExporter {
public:
    static bool to_json(const SarRecord& sar, char* buffer, size_t buffer_size) noexcept {
        if (!buffer || buffer_size == 0) return false;
        const char* type_str = "UNKNOWN";
        switch (sar.activity_type) {
            case SuspiciousActivityType::kStructuring: type_str = "STRUCTURING_BELOW_CTR_THRESHOLD"; break;
            case SuspiciousActivityType::kWashTrading: type_str = "WASH_TRADING_SELF_CROSS"; break;
            case SuspiciousActivityType::kLayeringSpoofing: type_str = "LAYERING_MARKET_MANIPULATION"; break;
            case SuspiciousActivityType::kVelocityAnomaly: type_str = "ABNORMAL_TRANSACTION_VELOCITY"; break;
            case SuspiciousActivityType::kSanctionsViolation: type_str = "OFAC_SANCTIONS_MATCH"; break;
            case SuspiciousActivityType::kKycLimitBreach: type_str = "UNAUTHORIZED_TIER_LIMIT_BREACH"; break;
            case SuspiciousActivityType::kNone: type_str = "NONE"; break;
        }

        const int written = std::snprintf(
            buffer, buffer_size,
            "{\n"
            "  \"regulatory_filing\": \"FinCEN_SAR_FORM_111\",\n"
            "  \"sar_id\": %llu,\n"
            "  \"filing_timestamp_ns\": %llu,\n"
            "  \"suspect_subject\": {\n"
            "    \"trader_id\": %u\n"
            "  },\n"
            "  \"suspicious_activity\": {\n"
            "    \"category\": \"%s\",\n"
            "    \"amount_cents\": %llu,\n"
            "    \"event_occurrences\": %u,\n"
            "    \"narrative\": \"%s\"\n"
            "  }\n"
            "}\n",
            static_cast<unsigned long long>(sar.sar_id),
            static_cast<unsigned long long>(sar.timestamp_ns),
            static_cast<unsigned>(sar.suspect_trader_id),
            type_str,
            static_cast<unsigned long long>(sar.suspicious_amount),
            static_cast<unsigned>(sar.occurrences),
            sar.narrative);
        return written > 0 && static_cast<size_t>(written) < buffer_size;
    }
};

template <uint16_t MaxTraders = 1024>
class ComplianceRegistry {
public:
    ComplianceRegistry() noexcept {
        for (uint16_t i = 0; i < MaxTraders; ++i) {
            _profiles[i].trader_id = i;
        }
    }

    bool register_trader(uint16_t trader_id, KycTier tier, bool sanctioned = false) noexcept {
        if (trader_id >= MaxTraders) return false;
        _profiles[trader_id].tier = tier;
        _profiles[trader_id].ofac_sanctioned = sanctioned;
        _profiles[trader_id].suspended = false;
        _profiles[trader_id].daily_notional_traded = 0;
        return true;
    }

    void set_sanctioned(uint16_t trader_id, bool sanctioned) noexcept {
        if (trader_id < MaxTraders) {
            _profiles[trader_id].ofac_sanctioned = sanctioned;
        }
    }

    void set_suspended(uint16_t trader_id, bool suspended) noexcept {
        if (trader_id < MaxTraders) {
            _profiles[trader_id].suspended = suspended;
        }
    }

    [[nodiscard]] const CustomerProfile* get_profile(uint16_t trader_id) const noexcept {
        if (trader_id >= MaxTraders) return nullptr;
        return &_profiles[trader_id];
    }

    [[nodiscard]] bool validate_order(uint16_t trader_id, uint64_t notional, uint64_t now_ns,
                                      SarRecord* out_sar = nullptr) noexcept {
        if (trader_id >= MaxTraders) {
            if (out_sar) populate_sar(*out_sar, trader_id, SuspiciousActivityType::kKycLimitBreach,
                                      notional, now_ns, "Unregistered trader ID attempted order entry");
            return false;
        }

        CustomerProfile& profile = _profiles[trader_id];
        if (profile.ofac_sanctioned) {
            if (out_sar) populate_sar(*out_sar, trader_id, SuspiciousActivityType::kSanctionsViolation,
                                      notional, now_ns, "Blocked: OFAC Sanctions list exact match");
            return false;
        }

        if (profile.suspended) {
            return false;
        }

        if (profile.tier == KycTier::kUnverified) {
            if (out_sar) populate_sar(*out_sar, trader_id, SuspiciousActivityType::kKycLimitBreach,
                                      notional, now_ns, "Order rejected: Customer KYC unverified (Tier 0)");
            return false;
        }

        const KycLimits limits = get_kyc_limits(profile.tier);
        if (notional > limits.max_single_notional) {
            if (out_sar) populate_sar(*out_sar, trader_id, SuspiciousActivityType::kKycLimitBreach,
                                      notional, now_ns, "Order exceeds maximum single-order KYC tier limit");
            return false;
        }

        constexpr uint64_t kDayNs = 86'400ULL * 1'000'000'000ULL;
        if (now_ns >= profile.last_day_reset_ns + kDayNs) {
            profile.daily_notional_traded = 0;
            profile.last_day_reset_ns = now_ns;
        }

        if (profile.daily_notional_traded + notional > limits.max_daily_notional) {
            if (out_sar) populate_sar(*out_sar, trader_id, SuspiciousActivityType::kKycLimitBreach,
                                      notional, now_ns, "Order exceeds 24-hour aggregate KYC tier volume");
            return false;
        }

        profile.daily_notional_traded += notional;
        return true;
    }

private:
    void populate_sar(SarRecord& sar, uint16_t trader_id, SuspiciousActivityType type,
                      uint64_t amount, uint64_t now_ns, const char* reason) const noexcept {
        sar.sar_id = _next_sar_id.fetch_add(1, std::memory_order_relaxed) + 1;
        sar.timestamp_ns = now_ns;
        sar.suspect_trader_id = trader_id;
        sar.activity_type = type;
        sar.suspicious_amount = amount;
        sar.occurrences = 1;
        std::strncpy(sar.narrative, reason ? reason : "", sizeof(sar.narrative) - 1);
        sar.narrative[sizeof(sar.narrative) - 1] = '\0';
    }

    std::array<CustomerProfile, MaxTraders> _profiles{};
    mutable std::atomic<uint64_t> _next_sar_id{1000};
};

template <uint16_t MaxTraders = 1024>
class TransactionMonitoringEngine {
public:
    static constexpr uint64_t kCtrThreshold = 10'000 * 10'000ULL;
    static constexpr uint64_t kStructuringMin = 8'500 * 10'000ULL;
    static constexpr uint64_t kStructuringMax = 9'999 * 10'000ULL;
    static constexpr uint32_t kStructuringTriggerCount = 3;

    TransactionMonitoringEngine() noexcept = default;

    [[nodiscard]] bool check_structuring(uint16_t trader_id, uint64_t amount, uint64_t now_ns,
                                         SarRecord* out_sar = nullptr) noexcept {
        if (trader_id >= MaxTraders) return false;

        if (amount >= kStructuringMin && amount <= kStructuringMax) {
            TraderStructuringState& state = _structuring_states[trader_id];
            constexpr uint64_t kWindowNs = 86'400ULL * 1'000'000'000ULL;
            if (now_ns >= state.window_start_ns + kWindowNs) {
                state.window_start_ns = now_ns;
                state.count = 0;
                state.accumulated_amount = 0;
            }
            ++state.count;
            state.accumulated_amount += amount;

            if (state.count >= kStructuringTriggerCount) {
                if (out_sar) {
                    out_sar->sar_id = _sar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
                    out_sar->timestamp_ns = now_ns;
                    out_sar->suspect_trader_id = trader_id;
                    out_sar->activity_type = SuspiciousActivityType::kStructuring;
                    out_sar->suspicious_amount = state.accumulated_amount;
                    out_sar->occurrences = state.count;
                    std::snprintf(out_sar->narrative, sizeof(out_sar->narrative),
                                  "Multiple structured transactions below $10,000 threshold within 24 hours");
                }
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool check_wash_trade(uint16_t buy_trader_id, uint16_t sell_trader_id,
                                        uint64_t quantity, int64_t price, uint64_t now_ns,
                                        SarRecord* out_sar = nullptr) noexcept {
        if (buy_trader_id == sell_trader_id && buy_trader_id != 0) {
            if (out_sar) {
                out_sar->sar_id = _sar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
                out_sar->timestamp_ns = now_ns;
                out_sar->suspect_trader_id = buy_trader_id;
                out_sar->activity_type = SuspiciousActivityType::kWashTrading;
                out_sar->suspicious_amount = quantity * static_cast<uint64_t>(price > 0 ? price : 0);
                out_sar->occurrences = 1;
                std::snprintf(out_sar->narrative, sizeof(out_sar->narrative),
                              "Direct self-cross wash trade detected on same trader account");
            }
            return true;
        }
        return false;
    }

private:
    struct TraderStructuringState {
        uint64_t window_start_ns = 0;
        uint32_t count = 0;
        uint64_t accumulated_amount = 0;
    };

    std::array<TraderStructuringState, MaxTraders> _structuring_states{};
    std::atomic<uint64_t> _sar_counter{5000};
};

struct ComplianceAuditEvent {
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    uint16_t trader_id = 0;
    uint8_t event_type = 0;
    uint8_t activity_type = 0;
    uint8_t _pad[4]{};
    uint64_t notional_amount = 0;
    uint8_t previous_hash[32]{};
    uint8_t hash[32]{};
};
static_assert(sizeof(ComplianceAuditEvent) == 96, "ComplianceAuditEvent layout changed");

class ComplianceAuditLedger {
public:
    ComplianceAuditLedger() = default;
    ~ComplianceAuditLedger() { close(); }
    ComplianceAuditLedger(const ComplianceAuditLedger&) = delete;
    ComplianceAuditLedger& operator=(const ComplianceAuditLedger&) = delete;

    [[nodiscard]] bool open(const char* path) noexcept {
        if (!path || _fd >= 0) return false;
        _fd = ::open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (_fd < 0) return false;

        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        (void)::fcntl(_fd, F_SETLK, &fl);

        struct stat st{};
        if (::fstat(_fd, &st) != 0 || st.st_size % static_cast<off_t>(sizeof(ComplianceAuditEvent)) != 0) {
            close();
            return false;
        }

        _sequence = 0;
        _tail.fill(0);
        const uint64_t total = static_cast<uint64_t>(st.st_size / static_cast<off_t>(sizeof(ComplianceAuditEvent)));
        for (uint64_t seq = 0; seq < total; ++seq) {
            ComplianceAuditEvent ev{};
            if (::pread(_fd, &ev, sizeof(ComplianceAuditEvent), static_cast<off_t>(seq * sizeof(ComplianceAuditEvent))) !=
                static_cast<ssize_t>(sizeof(ComplianceAuditEvent))) {
                close();
                return false;
            }
            if (ev.sequence != seq || std::memcmp(ev.previous_hash, _tail.data(), 32) != 0) {
                close();
                return false;
            }
            std::memcpy(_tail.data(), ev.hash, 32);
            _sequence = seq + 1;
        }
        return true;
    }

    void close() noexcept {
        if (_fd >= 0) {
            ::fsync(_fd);
            struct flock fl{};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            (void)::fcntl(_fd, F_SETLK, &fl);
            ::close(_fd);
            _fd = -1;
        }
    }

    [[nodiscard]] bool log_event(uint64_t timestamp_ns, uint16_t trader_id, uint8_t event_type,
                                 uint8_t activity_type, uint64_t notional) noexcept {
        if (_fd < 0) return false;
        ComplianceAuditEvent ev{};
        ev.sequence = _sequence++;
        ev.timestamp_ns = timestamp_ns;
        ev.trader_id = trader_id;
        ev.event_type = event_type;
        ev.activity_type = activity_type;
        ev.notional_amount = notional;
        std::memcpy(ev.previous_hash, _tail.data(), 32);

        compute_hash(ev, ev.hash);
        std::memcpy(_tail.data(), ev.hash, 32);

        if (::write(_fd, &ev, sizeof(ComplianceAuditEvent)) != static_cast<ssize_t>(sizeof(ComplianceAuditEvent))) {
            return false;
        }
        ::fsync(_fd);
        return true;
    }

    [[nodiscard]] static bool verify_integrity(const char* log_path, uint64_t& verified_records,
                                               uint8_t* out_root_hash = nullptr) noexcept {
        verified_records = 0;
        if (!log_path) return false;
        int fd = ::open(log_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;

        struct stat st{};
        if (::fstat(fd, &st) != 0 || st.st_size % static_cast<off_t>(sizeof(ComplianceAuditEvent)) != 0) {
            ::close(fd);
            return false;
        }

        const uint64_t total = static_cast<uint64_t>(st.st_size / static_cast<off_t>(sizeof(ComplianceAuditEvent)));
        std::array<uint8_t, 32> prev_hash{};
        for (uint64_t seq = 0; seq < total; ++seq) {
            ComplianceAuditEvent ev{};
            if (::pread(fd, &ev, sizeof(ComplianceAuditEvent), static_cast<off_t>(seq * sizeof(ComplianceAuditEvent))) !=
                static_cast<ssize_t>(sizeof(ComplianceAuditEvent))) {
                ::close(fd);
                return false;
            }
            if (ev.sequence != seq || std::memcmp(ev.previous_hash, prev_hash.data(), 32) != 0) {
                ::close(fd);
                return false;
            }
            uint8_t expected_hash[32]{};
            compute_hash(ev, expected_hash);
            if (std::memcmp(ev.hash, expected_hash, 32) != 0) {
                ::close(fd);
                return false;
            }
            std::memcpy(prev_hash.data(), ev.hash, 32);
            ++verified_records;
        }
        if (out_root_hash) {
            std::memcpy(out_root_hash, prev_hash.data(), 32);
        }
        ::close(fd);
        return true;
    }

    [[nodiscard]] uint64_t sequence() const noexcept { return _sequence; }
    [[nodiscard]] const uint8_t* tail_hash() const noexcept { return _tail.data(); }

private:
    static void compute_hash(const ComplianceAuditEvent& ev, uint8_t* out_hash) noexcept {
#if defined(__APPLE__)
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, &ev, offsetof(ComplianceAuditEvent, hash));
        CC_SHA256_Final(out_hash, &ctx);
#else
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, &ev, offsetof(ComplianceAuditEvent, hash));
        SHA256_Final(out_hash, &ctx);
#endif
    }

    int _fd = -1;
    uint64_t _sequence = 0;
    std::array<uint8_t, 32> _tail{};
};

} // namespace luv
