#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <cstring>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace luv {

enum class AuthResult : uint8_t {
    Success = 0,
    InvalidSignature = 1,
    ReplayDetected = 2,
    RateLimitExceeded = 3,
    IPUnauthorized = 4,
    SessionTerminated = 5
};

struct SessionSecurityConfig {
    uint64_t session_id{0};
    uint64_t mpid{0};
    uint32_t authorized_ipv4{0};       // Network byte order IPv4
    uint32_t max_messages_per_sec{1000};
    uint32_t max_burst_allowance{100};
    uint8_t secret_key[32]{};          // 256-bit HMAC secret key
};

class SessionSecurityGuard {
public:
    static constexpr size_t kReplayWindowBits = 64;

    explicit SessionSecurityGuard(const SessionSecurityConfig& config) noexcept
        : config_(config), max_seq_seen_(0), replay_bitmap_(0),
          token_bucket_(static_cast<double>(config.max_burst_allowance)),
          last_token_update_ns_(0), is_active_(true) {}

    // Constant-time comparison to prevent timing side-channel attacks
    static bool constant_time_memcmp(const void* a, const void* b, size_t len) noexcept {
        const auto* pa = static_cast<const uint8_t*>(a);
        const auto* pb = static_cast<const uint8_t*>(b);
        uint8_t diff = 0;
        for (size_t i = 0; i < len; ++i) {
            diff |= (pa[i] ^ pb[i]);
        }
        return (diff == 0);
    }

    // Authenticate and validate incoming wire packet
    AuthResult validate_inbound_message(
        uint32_t peer_ipv4,
        uint64_t seq_num,
        const uint8_t* payload,
        size_t payload_len,
        const uint8_t* hmac_signature,
        uint64_t now_ns) noexcept
    {
        if (!is_active_) return AuthResult::SessionTerminated;

        // 1. IP Authorization check
        if (config_.authorized_ipv4 != 0 && peer_ipv4 != config_.authorized_ipv4) {
            return AuthResult::IPUnauthorized;
        }

        // 2. Token Bucket Rate Limiting
        if (!consume_rate_limit_token(now_ns)) {
            return AuthResult::RateLimitExceeded;
        }

        // 3. RFC 6479 Anti-Replay Sliding Window Protection
        if (seq_num == 0) return AuthResult::ReplayDetected;

        if (seq_num > max_seq_seen_) {
            uint64_t diff = seq_num - max_seq_seen_;
            if (diff < kReplayWindowBits) {
                replay_bitmap_ = (replay_bitmap_ << diff) | 1ULL;
            } else {
                replay_bitmap_ = 1ULL;
            }
            max_seq_seen_ = seq_num;
        } else {
            uint64_t diff = max_seq_seen_ - seq_num;
            if (diff >= kReplayWindowBits) {
                return AuthResult::ReplayDetected; // Too old, fallen off window
            }
            uint64_t bit_mask = (1ULL << diff);
            if ((replay_bitmap_ & bit_mask) != 0) {
                return AuthResult::ReplayDetected; // Duplicate message replayed
            }
            replay_bitmap_ |= bit_mask;
        }

        // 4. HMAC-SHA256 Constant-Time Signature Verification
        if (hmac_signature != nullptr) {
            uint8_t expected_mac[SHA256_DIGEST_LENGTH];
            unsigned int mac_len = SHA256_DIGEST_LENGTH;

            HMAC(EVP_sha256(), config_.secret_key, sizeof(config_.secret_key),
                 payload, payload_len, expected_mac, &mac_len);

            if (!constant_time_memcmp(hmac_signature, expected_mac, SHA256_DIGEST_LENGTH)) {
                return AuthResult::InvalidSignature;
            }
        }

        return AuthResult::Success;
    }

    void terminate_session() noexcept {
        is_active_ = false;
    }

    bool is_active() const noexcept { return is_active_; }
    uint64_t max_sequence_seen() const noexcept { return max_seq_seen_; }

private:
    SessionSecurityConfig config_{};
    uint64_t max_seq_seen_{0};
    uint64_t replay_bitmap_{0};
    double token_bucket_{100.0};
    uint64_t last_token_update_ns_{0};
    bool is_active_{true};

    bool consume_rate_limit_token(uint64_t now_ns) noexcept {
        if (last_token_update_ns_ == 0) {
            last_token_update_ns_ = now_ns;
        } else if (now_ns > last_token_update_ns_) {
            double elapsed_sec = static_cast<double>(now_ns - last_token_update_ns_) / 1e9;
            token_bucket_ = std::min(
                static_cast<double>(config_.max_burst_allowance),
                token_bucket_ + elapsed_sec * static_cast<double>(config_.max_messages_per_sec)
            );
            last_token_update_ns_ = now_ns;
        }

        if (token_bucket_ >= 1.0) {
            token_bucket_ -= 1.0;
            return true;
        }
        return false;
    }
};

} // namespace luv
