#include "luv_chaos.hpp"
#include "luv_safety.hpp"
#include "luv_lob.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

void test_chaos_packet_loss_detection() {
    luv::chaos::ChaosConfig config;
    config.packet_drop_rate = 0.20; // 20% drop rate
    config.seed = 12345;

    luv::chaos::NetworkChaosInjector injector(config);
    luv::SequenceTracker tracker;

    uint64_t emitted_count = 0;
    uint64_t gaps_detected = 0;

    for (uint64_t seq = 1; seq <= 100; ++seq) {
        uint8_t payload[8];
        std::memcpy(payload, &seq, sizeof(seq));

        injector.inject_and_process(payload, sizeof(payload), seq * 1000, [&](const uint8_t* data, size_t, uint64_t) {
            uint64_t s = 0;
            std::memcpy(&s, data, sizeof(s));
            emitted_count++;

            auto res = tracker.observe(s);
            if (res == luv::SequenceResult::kGap) {
                gaps_detected++;
            }
        });
    }

    assert(injector.dropped_count() > 0);
    assert(gaps_detected > 0);
    assert(emitted_count + injector.dropped_count() == 100);

    std::printf("[PASS] test_chaos_packet_loss_detection (Dropped: %llu, Gaps: %llu)\n",
        static_cast<unsigned long long>(injector.dropped_count()),
        static_cast<unsigned long long>(gaps_detected));
}

void test_chaos_reordering_and_duplication() {
    luv::chaos::ChaosConfig config;
    config.packet_reorder_rate = 0.30;
    config.packet_duplicate_rate = 0.15;
    config.reorder_depth = 5;
    config.seed = 54321;

    luv::chaos::NetworkChaosInjector injector(config);
    luv::SequenceTracker tracker;

    uint64_t duplicates_detected = 0;
    uint64_t out_of_order_detected = 0;

    for (uint64_t seq = 1; seq <= 200; ++seq) {
        uint8_t payload[8];
        std::memcpy(payload, &seq, sizeof(seq));

        injector.inject_and_process(payload, sizeof(payload), seq * 1000, [&](const uint8_t* data, size_t, uint64_t) {
            uint64_t s = 0;
            std::memcpy(&s, data, sizeof(s));

            auto res = tracker.observe(s);
            if (res == luv::SequenceResult::kDuplicate) {
                duplicates_detected++;
            } else if (res == luv::SequenceResult::kOutOfOrder) {
                out_of_order_detected++;
            }
        });
    }

    injector.flush([&](const uint8_t* data, size_t, uint64_t) {
        uint64_t s = 0;
        std::memcpy(&s, data, sizeof(s));
        auto res = tracker.observe(s);
        if (res == luv::SequenceResult::kDuplicate) duplicates_detected++;
        else if (res == luv::SequenceResult::kOutOfOrder) out_of_order_detected++;
    });

    assert(injector.reordered_count() > 0);
    assert(injector.duplicated_count() > 0);
    assert(duplicates_detected > 0 || out_of_order_detected > 0);

    std::printf("[PASS] test_chaos_reordering_and_duplication (Reordered: %llu, Duplicated: %llu)\n",
        static_cast<unsigned long long>(injector.reordered_count()),
        static_cast<unsigned long long>(injector.duplicated_count()));
}

void test_chaos_payload_corruption() {
    luv::chaos::ChaosConfig config;
    config.payload_corruption_rate = 0.50;
    config.seed = 999;

    luv::chaos::NetworkChaosInjector injector(config);
    uint64_t corrupted_received = 0;

    for (uint64_t seq = 1; seq <= 50; ++seq) {
        uint8_t payload[8] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
        injector.inject_and_process(payload, sizeof(payload), seq * 1000, [&](const uint8_t* data, size_t len, uint64_t) {
            bool all_aa = true;
            for (size_t i = 0; i < len; ++i) {
                if (data[i] != 0xAA) all_aa = false;
            }
            if (!all_aa) corrupted_received++;
        });
    }

    assert(injector.corrupted_count() > 0);
    assert(corrupted_received == injector.corrupted_count());

    std::printf("[PASS] test_chaos_payload_corruption (Corrupted: %llu)\n",
        static_cast<unsigned long long>(corrupted_received));
}

int main() {
    test_chaos_packet_loss_detection();
    test_chaos_reordering_and_duplication();
    test_chaos_payload_corruption();
    std::printf("All chaos network feed tests passed successfully.\n");
    return 0;
}
