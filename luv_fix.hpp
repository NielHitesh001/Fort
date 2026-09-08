#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <array>
#include "luv_execution.hpp"

namespace luv {
namespace fix {

// Standard FIX tag definitions
namespace Tag {
    constexpr uint32_t BeginString        = 8;
    constexpr uint32_t BodyLength         = 9;
    constexpr uint32_t CheckSum           = 10;
    constexpr uint32_t ClOrdID            = 11;
    constexpr uint32_t CumQty             = 14;
    constexpr uint32_t ExecID             = 17;
    constexpr uint32_t MsgSeqNum          = 34;
    constexpr uint32_t MsgType            = 35;
    constexpr uint32_t OrderID            = 37;
    constexpr uint32_t OrderQty           = 38;
    constexpr uint32_t OrdStatus          = 39;
    constexpr uint32_t OrdType            = 40;
    constexpr uint32_t OrigClOrdID        = 41;
    constexpr uint32_t Price              = 44;
    constexpr uint32_t SenderCompID       = 49;
    constexpr uint32_t SendingTime        = 52;
    constexpr uint32_t Side               = 54;
    constexpr uint32_t Symbol             = 55;
    constexpr uint32_t TargetCompID       = 56;
    constexpr uint32_t Text               = 58;
    constexpr uint32_t TimeInForce        = 59;
    constexpr uint32_t TransactTime       = 60;
    constexpr uint32_t ExecType           = 150;
    constexpr uint32_t LeavesQty          = 151;
    constexpr uint32_t HeartBtInt         = 108;
    constexpr uint32_t TestReqID          = 112;
    constexpr uint32_t RefSeqNum          = 45;
    constexpr uint32_t SessionRejectReason = 373;
} // namespace Tag

enum class SessionState : uint8_t {
    kDisconnected = 0,
    kLogonSent = 1,
    kActive = 2,
    kLogoutSent = 3
};

struct FixField {
    uint32_t tag = 0;
    std::string_view val;
};

// Zero-allocation FIX message view
class FixMessageView {
public:
    static constexpr size_t kMaxFields = 64;

    FixMessageView() noexcept : field_count_(0), raw_len_(0) {}

    bool parse(const char* buf, size_t len) noexcept {
        field_count_ = 0;
        raw_len_ = len;
        if (!buf || len < 10) return false;

        const char* ptr = buf;
        const char* end = buf + len;

        // Verify BeginString "8=FIX..."
        if (ptr[0] != '8' || ptr[1] != '=') return false;

        // Parse fields separated by SOH (0x01) or standard delimiter
        while (ptr < end && field_count_ < kMaxFields) {
            const char* tag_start = ptr;
            const char* eq = static_cast<const char*>(std::memchr(tag_start, '=', end - tag_start));
            if (!eq) break;

            uint32_t tag = 0;
            for (const char* t = tag_start; t < eq; ++t) {
                if (*t < '0' || *t > '9') return false;
                tag = tag * 10 + static_cast<uint32_t>(*t - '0');
            }

            const char* val_start = eq + 1;
            const char* delim = static_cast<const char*>(std::memchr(val_start, '\x01', end - val_start));
            if (!delim) {
                delim = static_cast<const char*>(std::memchr(val_start, '|', end - val_start));
            }
            if (!delim) {
                delim = end;
            }

            fields_[field_count_++] = FixField{tag, std::string_view(val_start, static_cast<size_t>(delim - val_start))};

            ptr = (delim < end) ? delim + 1 : end;
        }

        return field_count_ > 0;
    }

    std::string_view get(uint32_t tag) const noexcept {
        for (size_t i = 0; i < field_count_; ++i) {
            if (fields_[i].tag == tag) return fields_[i].val;
        }
        return {};
    }

    int64_t get_int(uint32_t tag, int64_t default_val = 0) const noexcept {
        auto sv = get(tag);
        if (sv.empty()) return default_val;
        int64_t val = 0;
        bool neg = false;
        size_t idx = 0;
        if (sv[0] == '-') { neg = true; idx = 1; }
        for (; idx < sv.size(); ++idx) {
            if (sv[idx] < '0' || sv[idx] > '9') break;
            val = val * 10 + (sv[idx] - '0');
        }
        return neg ? -val : val;
    }

    char get_char(uint32_t tag, char default_val = '\0') const noexcept {
        auto sv = get(tag);
        if (sv.empty()) return default_val;
        return sv[0];
    }

    std::string_view msg_type() const noexcept {
        return get(Tag::MsgType);
    }

    uint64_t seq_num() const noexcept {
        return static_cast<uint64_t>(get_int(Tag::MsgSeqNum, 0));
    }

    // Verify Checksum Tag 10
    static bool verify_checksum(const char* buf, size_t len) noexcept {
        if (!buf || len < 7) return false;
        // Search backwards for "\x0110=" or "|10="
        const char* ck_tag = nullptr;
        for (ssize_t i = static_cast<ssize_t>(len) - 8; i >= 0; --i) {
            if ((buf[i] == '\x01' || buf[i] == '|') && buf[i+1] == '1' && buf[i+2] == '0' && buf[i+3] == '=') {
                ck_tag = buf + i + 1;
                break;
            }
        }
        if (!ck_tag) return false;

        uint32_t sum = 0;
        for (const char* p = buf; p < ck_tag; ++p) {
            sum += static_cast<uint8_t>(*p);
        }
        const uint32_t expected = sum % 256;

        const char* val = ck_tag + 3;
        uint32_t actual = 0;
        for (int i = 0; i < 3 && val[i] >= '0' && val[i] <= '9'; ++i) {
            actual = actual * 10 + static_cast<uint32_t>(val[i] - '0');
        }
        return expected == actual;
    }

private:
    std::array<FixField, kMaxFields> fields_{};
    size_t field_count_{0};
    size_t raw_len_{0};
};

// FIX Session Manager
class FixSession {
public:
    FixSession(std::string_view sender_comp_id, std::string_view target_comp_id) noexcept
        : sender_comp_id_(sender_comp_id), target_comp_id_(target_comp_id) {}

    SessionState state() const noexcept { return state_; }
    uint64_t next_in_seq() const noexcept { return next_in_seq_; }
    uint64_t next_out_seq() const noexcept { return next_out_seq_; }

    bool handle_inbound_seq(uint64_t seq_num, bool& out_gap_detected, uint64_t& out_expected_seq) noexcept {
        out_gap_detected = false;
        if (seq_num > next_in_seq_) {
            out_gap_detected = true;
            out_expected_seq = next_in_seq_;
            return false;
        }
        if (seq_num == next_in_seq_) {
            next_in_seq_++;
            return true;
        }
        // Sequence number lower than expected (duplicate)
        return false;
    }

    void on_logon() noexcept { state_ = SessionState::kActive; }
    void on_logout() noexcept { state_ = SessionState::kDisconnected; }

    // Formats ExecutionReport (35=8) into output buffer
    size_t format_execution_report(
        char* out_buf,
        size_t max_len,
        uint64_t clordid,
        uint64_t order_id,
        uint64_t exec_id,
        char exec_type, // '0'=New, '1'=Partial, '2'=Fill, '4'=Cancelled, '8'=Rejected
        char ord_status,
        std::string_view symbol,
        char side, // '1'=Buy, '2'=Sell
        int64_t order_qty,
        int64_t price,
        int64_t cum_qty,
        int64_t leaves_qty) noexcept
    {
        if (!out_buf || max_len < 128) return 0;

        char body[512];
        const uint64_t seq = next_out_seq_++;
        const int body_len = std::snprintf(
            body, sizeof(body),
            "35=8\x01"
            "49=%.*s\x01"
            "56=%.*s\x01"
            "34=%llu\x01"
            "52=20260908-00:00:00.000\x01"
            "11=%llu\x01"
            "37=%llu\x01"
            "17=%llu\x01"
            "150=%c\x01"
            "39=%c\x01"
            "55=%.*s\x01"
            "54=%c\x01"
            "38=%lld\x01"
            "44=%lld\x01"
            "14=%lld\x01"
            "151=%lld\x01",
            static_cast<int>(sender_comp_id_.size()), sender_comp_id_.data(),
            static_cast<int>(target_comp_id_.size()), target_comp_id_.data(),
            static_cast<unsigned long long>(seq),
            static_cast<unsigned long long>(clordid),
            static_cast<unsigned long long>(order_id),
            static_cast<unsigned long long>(exec_id),
            exec_type,
            ord_status,
            static_cast<int>(symbol.size()), symbol.data(),
            side,
            static_cast<long long>(order_qty),
            static_cast<long long>(price),
            static_cast<long long>(cum_qty),
            static_cast<long long>(leaves_qty)
        );

        if (body_len <= 0) return 0;

        char prefix[64];
        const int prefix_len = std::snprintf(prefix, sizeof(prefix), "8=FIX.4.4\x01""9=%d\x01", body_len);
        if (prefix_len <= 0) return 0;

        const size_t total_payload = static_cast<size_t>(prefix_len + body_len);
        if (total_payload + 16 > max_len) return 0;

        std::memcpy(out_buf, prefix, prefix_len);
        std::memcpy(out_buf + prefix_len, body, body_len);

        uint32_t cksum = 0;
        for (size_t i = 0; i < total_payload; ++i) {
            cksum += static_cast<uint8_t>(out_buf[i]);
        }
        cksum %= 256;

        const int trailer_len = std::snprintf(out_buf + total_payload, max_len - total_payload, "10=%03u\x01", cksum);
        return total_payload + static_cast<size_t>(trailer_len);
    }

    // Formats Logon (35=A)
    size_t format_logon(char* out_buf, size_t max_len, uint32_t heartbeat_sec = 30) noexcept {
        if (!out_buf || max_len < 128) return 0;
        char body[256];
        const uint64_t seq = next_out_seq_++;
        const int body_len = std::snprintf(
            body, sizeof(body),
            "35=A\x01"
            "49=%.*s\x01"
            "56=%.*s\x01"
            "34=%llu\x01"
            "52=20260908-00:00:00.000\x01"
            "98=0\x01"
            "108=%u\x01",
            static_cast<int>(sender_comp_id_.size()), sender_comp_id_.data(),
            static_cast<int>(target_comp_id_.size()), target_comp_id_.data(),
            static_cast<unsigned long long>(seq),
            heartbeat_sec
        );

        char prefix[64];
        const int prefix_len = std::snprintf(prefix, sizeof(prefix), "8=FIX.4.4\x01""9=%d\x01", body_len);
        const size_t total_payload = static_cast<size_t>(prefix_len + body_len);

        std::memcpy(out_buf, prefix, prefix_len);
        std::memcpy(out_buf + prefix_len, body, body_len);

        uint32_t cksum = 0;
        for (size_t i = 0; i < total_payload; ++i) {
            cksum += static_cast<uint8_t>(out_buf[i]);
        }
        cksum %= 256;

        const int trailer_len = std::snprintf(out_buf + total_payload, max_len - total_payload, "10=%03u\x01", cksum);
        return total_payload + static_cast<size_t>(trailer_len);
    }

    // Formats Heartbeat (35=0)
    size_t format_heartbeat(char* out_buf, size_t max_len, std::string_view test_req_id = "") noexcept {
        if (!out_buf || max_len < 128) return 0;
        char body[256];
        const uint64_t seq = next_out_seq_++;
        int body_len = 0;
        if (!test_req_id.empty()) {
            body_len = std::snprintf(
                body, sizeof(body),
                "35=0\x01"
                "49=%.*s\x01"
                "56=%.*s\x01"
                "34=%llu\x01"
                "52=20260908-00:00:00.000\x01"
                "112=%.*s\x01",
                static_cast<int>(sender_comp_id_.size()), sender_comp_id_.data(),
                static_cast<int>(target_comp_id_.size()), target_comp_id_.data(),
                static_cast<unsigned long long>(seq),
                static_cast<int>(test_req_id.size()), test_req_id.data()
            );
        } else {
            body_len = std::snprintf(
                body, sizeof(body),
                "35=0\x01"
                "49=%.*s\x01"
                "56=%.*s\x01"
                "34=%llu\x01"
                "52=20260908-00:00:00.000\x01",
                static_cast<int>(sender_comp_id_.size()), sender_comp_id_.data(),
                static_cast<int>(target_comp_id_.size()), target_comp_id_.data(),
                static_cast<unsigned long long>(seq)
            );
        }

        char prefix[64];
        const int prefix_len = std::snprintf(prefix, sizeof(prefix), "8=FIX.4.4\x01""9=%d\x01", body_len);
        const size_t total_payload = static_cast<size_t>(prefix_len + body_len);

        std::memcpy(out_buf, prefix, prefix_len);
        std::memcpy(out_buf + prefix_len, body, body_len);

        uint32_t cksum = 0;
        for (size_t i = 0; i < total_payload; ++i) {
            cksum += static_cast<uint8_t>(out_buf[i]);
        }
        cksum %= 256;

        const int trailer_len = std::snprintf(out_buf + total_payload, max_len - total_payload, "10=%03u\x01", cksum);
        return total_payload + static_cast<size_t>(trailer_len);
    }

private:
    std::string_view sender_comp_id_;
    std::string_view target_comp_id_;
    SessionState state_{SessionState::kDisconnected};
    uint64_t next_in_seq_{1};
    uint64_t next_out_seq_{1};
};

} // namespace fix
} // namespace luv
