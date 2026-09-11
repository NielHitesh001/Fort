#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <limits>
#include "luv_arena.hpp"
#include "luv_decode_itch.hpp"
#include "luv_feed.hpp"
#include "luv_feed_sim.hpp"
// Not including luv_feed_dpdk.hpp — no DPDK libs in this build

// ─────────────────────────────────────────────────────────────────────────────
//  test_feed.cpp — Integration test
//
//  Validates the full pipeline:
//    1. Arena init (mmap + mlock)
//    2. SymbolTable insert + lookup
//    3. SimFeedSource synthetic generation → decode_itch() → tick_ring
//    4. Consumer reads decoded TickMsg from the ring
//    5. Memory report
// ─────────────────────────────────────────────────────────────────────────────

static void test_symbol_table() {
    printf("\n══ SymbolTable ══════════════════════════════════\n");
    luv::SymbolTable tab;

    // Insert a few tickers
    char aapl[8] = {'A','A','P','L',' ',' ',' ',' '};
    char msft[8] = {'M','S','F','T',' ',' ',' ',' '};
    char tsla[8] = {'T','S','L','A',' ',' ',' ',' '};
    char nope[8] = {'N','O','P','E',' ',' ',' ',' '};

    assert(tab.insert(aapl, 0));
    assert(tab.insert(msft, 1));
    assert(tab.insert(tsla, 2));

    assert(tab.lookup(aapl) == 0);
    assert(tab.lookup(msft) == 1);
    assert(tab.lookup(tsla) == 2);
    assert(tab.lookup(nope) == luv::SymbolTable::kNotFound);

    printf("  [OK] insert/lookup — 3 symbols, 1 miss\n");
    printf("  [OK] table size: %u\n", tab.size());
}

static void test_decode_itch() {
    printf("\n══ decode_itch() ════════════════════════════════\n");
    luv::SymbolTable tab;

    // Register "AAPL    " at index 42
    char ticker[8] = {'A','A','P','L',' ',' ',' ',' '};
    assert(tab.insert(ticker, 42));

    // Build a synthetic Add Order ('A') — 36 bytes, big-endian
    uint8_t msg[36] = {};
    msg[0] = 'A';  // msg_type

    // Stock Locate = 0 (bytes 1-2)
    msg[1] = 0; msg[2] = 0;

    // Tracking Number = 0 (bytes 3-4)
    msg[3] = 0; msg[4] = 0;

    // Timestamp = 1,000,000,000 ns (bytes 5-10, 48-bit big-endian)
    uint64_t ts = 1'000'000'000ULL;
    msg[5]  = static_cast<uint8_t>(ts >> 40);
    msg[6]  = static_cast<uint8_t>(ts >> 32);
    msg[7]  = static_cast<uint8_t>(ts >> 24);
    msg[8]  = static_cast<uint8_t>(ts >> 16);
    msg[9]  = static_cast<uint8_t>(ts >> 8);
    msg[10] = static_cast<uint8_t>(ts);

    // Order Ref = 0xDEADBEEF (bytes 11-18)
    uint64_t ref = 0xDEADBEEFULL;
    for (int i = 0; i < 8; ++i)
        msg[11 + i] = static_cast<uint8_t>(ref >> (56 - 8*i));

    // Buy/Sell = 'B' (byte 19)
    msg[19] = 'B';

    // Shares = 100 (bytes 20-23)
    uint32_t shares = 100;
    msg[20] = static_cast<uint8_t>(shares >> 24);
    msg[21] = static_cast<uint8_t>(shares >> 16);
    msg[22] = static_cast<uint8_t>(shares >> 8);
    msg[23] = static_cast<uint8_t>(shares);

    // Stock = "AAPL    " (bytes 24-31)
    std::memcpy(msg + 24, ticker, 8);

    // Price = 150.0000 = 1500000 (bytes 32-35)
    uint32_t price = 1'500'000;
    msg[32] = static_cast<uint8_t>(price >> 24);
    msg[33] = static_cast<uint8_t>(price >> 16);
    msg[34] = static_cast<uint8_t>(price >> 8);
    msg[35] = static_cast<uint8_t>(price);

    luv::TickMsg out{};
    bool ok = luv::decode_itch(msg, 36, tab, out);
    assert(ok);
    assert(out.msg_type == 'A');
    assert(out.symbol_idx == 42);
    assert(out.order_ref == 0xDEADBEEFULL);
    assert(out.qty == 100);
    assert(out.price == 1'500'000);
    assert(out.timestamp == 1'000'000'000ULL);
    assert((out.flags & luv::tick_flags::kBuy) != 0);

    printf("  [OK] Add Order decode — all fields verified\n");

       msg[23] = 0;
       assert(!luv::decode_itch(msg, 36, tab, out));
       msg[23] = 100;
       msg[19] = 'X';
       assert(!luv::decode_itch(msg, 36, tab, out));
       msg[19] = 'B';
       msg[5] = 0xFF; msg[6] = 0xFF; msg[7] = 0xFF;
       msg[8] = 0xFF; msg[9] = 0xFF; msg[10] = 0xFF;
       assert(!luv::decode_itch(msg, 36, tab, out));
       printf("  [OK] malformed quantity/side/timestamp rejected\n");

    // Test skip on unknown message type
    uint8_t unk[11] = {};
    unk[0] = 'Z';  // unknown type
    luv::TickMsg out2{};
    assert(!luv::decode_itch(unk, 11, tab, out2));
    printf("  [OK] Unknown message type correctly skipped\n");
}

static void test_sim_feed_pipeline() {
    printf("\n══ SimFeedSource pipeline ═══════════════════════\n");

    // 1. Init arena
    luv::Arena arena;
    if (!arena.init()) {
        printf("  [FAIL] arena.init() failed\n");
        return;
    }
    printf("  [OK] Arena initialised (mlocked=%s)\n",
           arena.is_mlocked() ? "yes" : "no");

    // 2. Configure sim feed — high rate, small buffer for test
    luv::SimConfig cfg;
    cfg.mode = luv::SimConfig::Mode::kSynthetic;
    cfg.synthetic_symbols = 64;
    cfg.target_rate_hz = 0;          // unlimited — run as fast as possible
    cfg.prebuf_count = 1024;         // small for test
    cfg.seed = 12345;

    luv::SimFeedSource feed(cfg);
    if (!feed.init(arena)) {
        printf("  [FAIL] feed.init() failed\n");
        return;
    }
    printf("  [OK] SimFeedSource initialised (synthetic, 64 symbols, 1024 prebuf)\n");

    // 3. Poll messages into the ring
    uint32_t total_decoded = 0;
    for (uint32_t i = 0; i < 10'000; ++i) {
        total_decoded += feed.poll();
    }
    printf("  [OK] Polled 10,000 iterations → %u messages decoded\n", total_decoded);
    printf("       feed.total_messages() = %llu\n",
           (unsigned long long)feed.total_messages());
    printf("       feed.total_bytes()    = %llu\n",
           (unsigned long long)feed.total_bytes());

    // 4. Consume some messages from the ring and verify structure
    uint32_t consumed = 0;
    uint32_t add_orders = 0, executes = 0, cancels = 0, deletes = 0;

    while (auto* tick = arena.tick_ring.try_peek()) {
        switch (tick->msg_type) {
            case 'A': case 'F': ++add_orders; break;
            case 'E': case 'C': ++executes;   break;
            case 'X':           ++cancels;     break;
            case 'D':           ++deletes;     break;
        }
        arena.tick_ring.consume();
        ++consumed;
        if (consumed >= 200) break;  // sample first 200
    }

    printf("  [OK] Consumed %u messages from tick_ring\n", consumed);
    printf("       Add orders: %u  Executes: %u  Cancels: %u  Deletes: %u\n",
           add_orders, executes, cancels, deletes);

    // 5. Verify message types are in expected distribution (rough check)
    assert(consumed > 0);
    assert(add_orders > 0);  // 60% should be adds
    printf("  [OK] Message type distribution looks reasonable\n");

    // 6. Memory report
    auto r = arena.report();
    printf("\n══ Memory Report ═══════════════════════════════\n");
    printf("  LOB:         %10zu bytes  (%.1f MiB)\n",
           r.lob_bytes, (double)r.lob_bytes / (1<<20));
    printf("  Tick ring:   %10zu bytes  (%.1f MiB)\n",
           r.tick_ring_bytes, (double)r.tick_ring_bytes / (1<<20));
    printf("  Features:    %10zu bytes  (%.1f MiB)\n",
           r.feature_bytes, (double)r.feature_bytes / (1<<20));
    printf("  Signals:     %10zu bytes  (%.1f KiB)\n",
           r.signal_bytes, (double)r.signal_bytes / 1024.0);
    printf("  Exec/Risk:   %10zu bytes  (%.1f MiB)\n",
           r.exec_bytes, (double)r.exec_bytes / (1<<20));
    printf("  Telemetry:   %10zu bytes  (%.1f MiB)\n",
           r.telem_bytes, (double)r.telem_bytes / (1<<20));
    printf("  WebSocket:   %10zu bytes  (%.1f MiB)\n",
           r.websocket_control_plane_bytes,
           (double)r.websocket_control_plane_bytes / (1<<20));
    printf("  ─────────────────────────────────────────────\n");
    printf("  Infra total: %10zu bytes  (%.1f MiB)\n",
           r.infra_total_bytes, (double)r.infra_total_bytes / (1<<20));
    printf("  AI region:   %10zu bytes  (%.1f GiB)\n",
           r.ai_region_bytes, (double)r.ai_region_bytes / (1ULL<<30));
    printf("  Grand total: %10zu bytes  (%.1f GiB)\n",
           r.grand_total_bytes, (double)r.grand_total_bytes / (1ULL<<30));
    printf("  mlocked:     %s\n", r.mlocked ? "yes" : "no");
}

static void test_fault_injection() {
       printf("\n══ SimFeedSource fault injection ═══════════════\n");

       for (double invalid_drop_rate : {1.5, -0.1}) {
              luv::Arena arena;
              assert(arena.init());
              luv::SimConfig invalid_cfg{};
              invalid_cfg.drop_rate = invalid_drop_rate;
              luv::SimFeedSource invalid_feed(invalid_cfg);
              assert(!invalid_feed.init(arena));
       }
       printf("  [OK] invalid drop rates fail closed\n");

       const double drop_rates[] = {0.0, 0.5, 1.0};
       for (double drop_rate : drop_rates) {
              luv::Arena arena;
              assert(arena.init());

              luv::SimConfig cfg{};
              cfg.synthetic_symbols = 8;
              cfg.target_rate_hz = 0;
              cfg.prebuf_count = 64;
              cfg.drop_rate = drop_rate;

              luv::SimFeedSource feed(cfg);
              assert(feed.init(arena));

              constexpr uint32_t attempts = 1'000;
              for (uint32_t i = 0; i < attempts; ++i) {
                     (void)feed.poll();
                     while (arena.tick_ring.try_peek()) arena.tick_ring.consume();
              }

              assert(feed.total_dropped() + feed.total_messages() == attempts);
              if (drop_rate == 0.0) assert(feed.total_dropped() == 0);
              if (drop_rate == 1.0) assert(feed.total_messages() == 0);
              printf("  [OK] drop_rate=%.1f dropped=%llu decoded=%llu\n",
                        drop_rate,
                        static_cast<unsigned long long>(feed.total_dropped()),
                        static_cast<unsigned long long>(feed.total_messages()));
       }

       const char* replay_path = "/tmp/luv-feed-drop-replay.bin";
       FILE* replay = std::fopen(replay_path, "wb");
       assert(replay != nullptr);
       const uint8_t replay_message[] = {
              0, 11, 'Z', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       };
       assert(std::fwrite(replay_message, 1, sizeof(replay_message), replay) ==
                 sizeof(replay_message));
       std::fclose(replay);

       luv::Arena arena;
       assert(arena.init());
       luv::SimConfig replay_cfg{};
       replay_cfg.mode = luv::SimConfig::Mode::kReplay;
       replay_cfg.replay_path = replay_path;
       replay_cfg.drop_rate = 1.0;
       replay_cfg.target_rate_hz = 0;
       luv::SimFeedSource replay_feed(replay_cfg);
       assert(replay_feed.init(arena));
       assert(replay_feed.poll() == 0);
       assert(replay_feed.total_dropped() == 1);
       assert(replay_feed.total_messages() == 0);
       std::remove(replay_path);
       printf("  [OK] replay drop path preserves sequential cursor\n");
}

static std::array<uint64_t, 64> collect_reorder_pass(uint64_t seed,
                                                                                              uint32_t reorder_window) {
       luv::Arena arena;
       assert(arena.init());
       luv::SimConfig cfg{};
       cfg.synthetic_symbols = 8;
       cfg.target_rate_hz = 0;
       cfg.prebuf_count = 64;
       cfg.seed = seed;
       cfg.reorder_window = reorder_window;

       luv::SimFeedSource feed(cfg);
       assert(feed.init(arena));
       std::array<uint64_t, 64> timestamps{};
       for (uint32_t i = 0; i < timestamps.size(); ++i) {
              assert(feed.poll() == 1);
              luv::TickMsg* message = arena.tick_ring.try_peek();
              assert(message != nullptr);
              timestamps[i] = message->timestamp;
              arena.tick_ring.consume();
       }
       assert(feed.total_messages() == timestamps.size());
       return timestamps;
}

static void test_reorder_multi_seed() {
       printf("\n══ Multi-seed reorder validation ═══════════════\n");
       const uint64_t seeds[] = {
              0,
              1,
              std::numeric_limits<uint64_t>::max(),
              0x1234'5678'9ABC'DEF0ULL,
              0xDEAD'BEEF'CAFE'BABEULL,
       };
       for (uint64_t seed : seeds) {
              const auto baseline = collect_reorder_pass(seed, 0);
              const auto reordered = collect_reorder_pass(seed, 63);
              auto expected = baseline;
              auto actual = reordered;
              std::sort(expected.begin(), expected.end());
              std::sort(actual.begin(), actual.end());
              assert(expected == actual);
              for (size_t i = 1; i < actual.size(); ++i) {
                     assert(actual[i - 1] != actual[i]);
              }
              printf("  [OK] seed=%llu full pass unique and complete\n",
                        static_cast<unsigned long long>(seed));
       }
}

int main() {
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  LUV Feed Integration Test                       ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    test_symbol_table();
    test_decode_itch();
    test_sim_feed_pipeline();
       test_fault_injection();
       test_reorder_multi_seed();

    printf("\n  ✅ All tests passed.\n\n");
    return 0;
}
