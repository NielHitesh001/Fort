#include "luv_session_security.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace luv;

void test_hmac_authentication_and_constant_time_verification() {
    SessionSecurityConfig cfg{};
    cfg.session_id = 999;
    cfg.authorized_ipv4 = 0x7F000001; // 127.0.0.1
    cfg.max_messages_per_sec = 1000;
    cfg.max_burst_allowance = 50;
    for (size_t i = 0; i < 32; ++i) cfg.secret_key[i] = static_cast<uint8_t>(i + 1);

    SessionSecurityGuard guard(cfg);

    const char* test_payload = "NEW_ORDER:SYMBOL=AAPL,QTY=100,PRICE=15000";
    size_t payload_len = std::strlen(test_payload);

    uint8_t valid_mac[SHA256_DIGEST_LENGTH];
    unsigned int mac_len = SHA256_DIGEST_LENGTH;
    HMAC(EVP_sha256(), cfg.secret_key, 32,
         reinterpret_cast<const uint8_t*>(test_payload), payload_len, valid_mac, &mac_len);

    // 1. Valid signature & IP
    auto res1 = guard.validate_inbound_message(
        0x7F000001, 1, reinterpret_cast<const uint8_t*>(test_payload), payload_len, valid_mac, 1'000'000'000ULL
    );
    assert(res1 == AuthResult::Success);

    // 2. Corrupted signature -> InvalidSignature
    uint8_t invalid_mac[SHA256_DIGEST_LENGTH];
    std::memcpy(invalid_mac, valid_mac, SHA256_DIGEST_LENGTH);
    invalid_mac[0] ^= 0xFF; // Tamper 1 byte
    auto res2 = guard.validate_inbound_message(
        0x7F000001, 2, reinterpret_cast<const uint8_t*>(test_payload), payload_len, invalid_mac, 1'001'000'000ULL
    );
    assert(res2 == AuthResult::InvalidSignature);

    // 3. Unauthorized IP
    auto res3 = guard.validate_inbound_message(
        0x0A000001, 3, reinterpret_cast<const uint8_t*>(test_payload), payload_len, valid_mac, 1'002'000'000ULL
    );
    assert(res3 == AuthResult::IPUnauthorized);
}

void test_replay_attack_and_sequence_protection() {
    SessionSecurityConfig cfg{};
    cfg.authorized_ipv4 = 0; // Accept all for test
    cfg.max_messages_per_sec = 10000;
    cfg.max_burst_allowance = 1000;

    SessionSecurityGuard guard(cfg);
    uint64_t now_ns = 1'000'000'000ULL;

    // Sequence 1: OK
    assert(guard.validate_inbound_message(0, 1, nullptr, 0, nullptr, now_ns) == AuthResult::Success);
    // Sequence 2: OK
    assert(guard.validate_inbound_message(0, 2, nullptr, 0, nullptr, now_ns) == AuthResult::Success);
    // Sequence 2 replayed -> ReplayDetected
    assert(guard.validate_inbound_message(0, 2, nullptr, 0, nullptr, now_ns) == AuthResult::ReplayDetected);
    // Sequence 1 replayed -> ReplayDetected
    assert(guard.validate_inbound_message(0, 1, nullptr, 0, nullptr, now_ns) == AuthResult::ReplayDetected);

    // Sequence 5 (out of order advance): OK
    assert(guard.validate_inbound_message(0, 5, nullptr, 0, nullptr, now_ns) == AuthResult::Success);
    // Sequence 3 (arrived late within window): OK
    assert(guard.validate_inbound_message(0, 3, nullptr, 0, nullptr, now_ns) == AuthResult::Success);
    // Sequence 3 again -> ReplayDetected
    assert(guard.validate_inbound_message(0, 3, nullptr, 0, nullptr, now_ns) == AuthResult::ReplayDetected);
}

void test_token_bucket_dos_rate_limiting() {
    SessionSecurityConfig cfg{};
    cfg.max_messages_per_sec = 100;
    cfg.max_burst_allowance = 5; // Allow only 5 burst tokens

    SessionSecurityGuard guard(cfg);
    uint64_t now_ns = 1'000'000'000ULL;

    for (int i = 1; i <= 5; ++i) {
        assert(guard.validate_inbound_message(0, i, nullptr, 0, nullptr, now_ns) == AuthResult::Success);
    }

    // 6th message in same instant exceeds burst bucket
    assert(guard.validate_inbound_message(0, 6, nullptr, 0, nullptr, now_ns) == AuthResult::RateLimitExceeded);
}

int main() {
    test_hmac_authentication_and_constant_time_verification();
    test_replay_attack_and_sequence_protection();
    test_token_bucket_dos_rate_limiting();
    std::cout << "Session Security & Authentication Guard tests passed.\n";
    return 0;
}
