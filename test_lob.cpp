#include <cassert>
#include <cstdio>

#include "luv_arena.hpp"
#include "luv_decode_itch.hpp"
#include "luv_lob.hpp"
#include "luv_consumer.hpp"
#include "luv_feed_sim.hpp"

namespace {

luv::TickMsg tick(uint8_t type, uint16_t sym, uint64_t ref, int64_t qty,
                  int64_t price = 0, uint8_t flags = 0,
                  uint64_t new_ref_or_match = 0) {
    luv::TickMsg t{};
    t.msg_type = type;
    t.symbol_idx = sym;
    t.order_ref = ref;
    t.qty = qty;
    t.price = price;
    t.flags = flags;
    t.match_num = new_ref_or_match;
    return t;
}

void push(luv::Arena& arena, const luv::TickMsg& t) {
    luv::TickMsg* slot = arena.tick_ring.try_claim();
    assert(slot != nullptr);
    *slot = t;
    arena.tick_ring.commit();
}

float feature_at(const luv::Arena& arena, uint16_t sym, uint32_t feature,
                 uint32_t step) {
    return arena.feature_rows[sym].data[
        feature * luv::Config::kLookbackSteps + step
    ];
}

void test_lob_reconstruction(luv::Arena& arena) {
    std::printf("\n== LOB reconstruction ==\n");

    luv::LOBEngine lob;
    assert(lob.init(arena));

    constexpr uint16_t sym = 0;
    constexpr int64_t px100 = 1'000'000;
    constexpr int64_t px101 = 1'010'000;
    constexpr int64_t px102 = 1'020'000;
    constexpr int64_t px103 = 1'030'000;

    lob.process(tick('A', sym, 1, 100, px100, luv::tick_flags::kBuy));
    assert(lob.best_bid_price(sym) == px100);
    assert(lob.bid_depth_qty(sym, 1) == 100);
    assert(arena.level(sym, 0, 0).order_count == 1);

    const uint64_t rejected_before = lob.rejected_events();
    lob.process(tick('A', sym, 99, -1, px100, luv::tick_flags::kBuy));
    lob.process(tick('?', sym, 99, 1, px100));
    assert(lob.rejected_events() == rejected_before + 2);

    lob.process(tick('A', sym, 2, 50, px101, luv::tick_flags::kBuy));
    assert(lob.best_bid_price(sym) == px101);
    assert(lob.bid_level_count(sym) == 2);
    assert(arena.level(sym, 0, 0).price == px101);
    assert(arena.level(sym, 0, 1).price == px100);

    lob.process(tick('A', sym, 3, 80, px103, 0));
    assert(lob.best_ask_price(sym) == px103);
    assert(lob.mid_price(sym) == (px101 + px103) / 2);
    assert(lob.spread(sym) == px103 - px101);

    lob.process(tick('E', sym, 2, 20));
    assert(arena.level(sym, 0, 0).total_qty == 30);
    assert(lob.bid_depth_qty(sym, 1) == 30);

    lob.process(tick('X', sym, 2, 30));
    assert(lob.best_bid_price(sym) == px100);
    assert(lob.bid_level_count(sym) == 1);

    lob.process(tick('D', sym, 1, 0));
    assert(lob.best_bid_price(sym) == 0);
    assert(lob.bid_level_count(sym) == 0);

    lob.process(tick('U', sym, 3, 70, px102, 0, 4));
    assert(lob.best_ask_price(sym) == px102);
    assert(lob.ask_level_count(sym) == 1);
    assert(arena.level(sym, 1, 0).total_qty == 70);
    assert(arena.level(sym, 1, 0).orders[0].order_id == 4);

    lob.process(tick('A', sym, 5, 10, px102, 0));
    arena.level(sym, 1, 0).orders[0].flags = 0;
    lob.process(tick('D', sym, 5, 0));
    assert(lob.active_order_count() == 1);

    std::printf("  [OK] add/execute/cancel/delete/replace path\n");
    std::printf("  [OK] corrupt slot location rejected without dereference\n");
}

void test_consumer_and_features() {
    std::printf("\n== Consumer + features ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::Consumer consumer;
    assert(consumer.init(arena));

    constexpr uint16_t sym = 1;
    push(arena, tick('A', luv::Config::kSymbols, 9, 100, 2'000'000,
                     luv::tick_flags::kBuy));
    assert(consumer.process_one());
    assert(consumer.rejected_ticks() == 1);
    assert(consumer.ticks_processed() == 0);

    push(arena, tick('A', sym, 10, 100, 2'000'000, luv::tick_flags::kBuy));
    push(arena, tick('A', sym, 11, 100, 2'020'000, 0));
    push(arena, tick('P', sym, 0, 25, 2'010'000,
                     luv::tick_flags::kPrintable | luv::tick_flags::kBuy,
                     999));

    assert(consumer.process_one());
    assert(consumer.process_one());
    assert(consumer.process_one());
    assert(consumer.ticks_processed() == 3);

    assert(consumer.lob().best_bid_price(sym) == 2'000'000);
    assert(consumer.lob().best_ask_price(sym) == 2'020'000);
    assert(consumer.lob().spread(sym) == 20'000);

    const uint32_t latest =
        (consumer.features().current_cursor(sym) + luv::Config::kLookbackSteps - 1)
        & (luv::Config::kLookbackSteps - 1);

    assert(feature_at(arena, sym, luv::feat::kImbal1, latest) > 0.0f);
    assert(feature_at(arena, sym, luv::feat::kSpreadRaw, latest) > 0.0f);
    assert(feature_at(arena, sym, luv::feat::kFlow1, latest) == 25.0f);

    std::printf("  [OK] ring consumption and feature writes\n");
}

void test_sim_feed_pipeline() {
    std::printf("\n== SimFeedSource -> Consumer smoke ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::SimConfig cfg;
    cfg.mode = luv::SimConfig::Mode::kSynthetic;
    cfg.synthetic_symbols = 16;
    cfg.target_rate_hz = 0;
    cfg.prebuf_count = 512;
    cfg.seed = 777;

    luv::SimFeedSource feed(cfg);
    assert(feed.init(arena));

    luv::Consumer consumer;
    assert(consumer.init(arena));

    uint32_t produced = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        produced += feed.poll();
        while (consumer.process_one()) {}
    }

    assert(produced > 0);
    assert(consumer.ticks_processed() > 0);

    bool any_feature = false;
    for (uint32_t sym = 0; sym < cfg.synthetic_symbols && !any_feature; ++sym) {
        const luv::FeatureRow& row = arena.feature_rows[sym];
        for (float v : row.data) {
            if (v != 0.0f) {
                any_feature = true;
                break;
            }
        }
    }
    assert(any_feature);

    std::printf("  [OK] produced=%u consumed=%llu features_nonzero=yes\n",
                produced,
                static_cast<unsigned long long>(consumer.ticks_processed()));
}

void test_fault_injected_feed() {
    std::printf("\n== Fault-injected SimFeedSource -> LOB ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::SimConfig cfg{};
    cfg.synthetic_symbols = 16;
    cfg.target_rate_hz = 0;
    cfg.prebuf_count = 512;
    cfg.seed = 0x1234;
    cfg.drop_rate = 0.35;
    cfg.reorder_window = 16;
    cfg.burst_size = 8;
    cfg.burst_interval = 32;

    luv::SimFeedSource feed(cfg);
    assert(feed.init(arena));
    luv::Consumer consumer;
    assert(consumer.init(arena));

    for (uint32_t i = 0; i < 5'000; ++i) {
        (void)feed.poll();
        while (consumer.process_one()) {}
    }
    while (consumer.process_one()) {}

    assert(feed.total_dropped() + feed.total_messages() == 5'000);
    assert(consumer.ticks_processed() == feed.total_messages());
    assert(consumer.lob().active_order_count() <
           luv::LOBEngine::order_ref_map_capacity());
    for (uint16_t sym = 0; sym < cfg.synthetic_symbols; ++sym) {
        assert(consumer.lob().bid_level_count(sym) <= luv::Config::kLevelsPerSide);
        assert(consumer.lob().ask_level_count(sym) <= luv::Config::kLevelsPerSide);
    }

    std::printf("  [OK] dropped=%llu decoded=%llu active_orders=%u\n",
                static_cast<unsigned long long>(feed.total_dropped()),
                static_cast<unsigned long long>(feed.total_messages()),
                consumer.lob().active_order_count());
}

void test_aggressive_fault_injected_feed() {
    std::printf("\n== Aggressive burst + reorder -> LOB ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::SimConfig cfg{};
    cfg.synthetic_symbols = 16;
    cfg.target_rate_hz = 0;
    cfg.prebuf_count = 64;
    cfg.seed = 0;
    cfg.drop_rate = 0.5;
    cfg.reorder_window = 63;
    cfg.burst_size = 64;
    cfg.burst_interval = 1;

    luv::SimFeedSource feed(cfg);
    assert(feed.init(arena));
    luv::Consumer consumer;
    assert(consumer.init(arena));

    constexpr uint32_t attempts = 2'000;
    for (uint32_t i = 0; i < attempts; ++i) {
        (void)feed.poll();
        while (consumer.process_one()) {}
    }
    while (consumer.process_one()) {}

    assert(feed.total_dropped() + feed.total_messages() == attempts);
    assert(consumer.ticks_processed() == feed.total_messages());
    assert(consumer.lob().active_order_count() <
           luv::LOBEngine::order_ref_map_capacity());
    std::printf("  [OK] attempts=%u dropped=%llu decoded=%llu active_orders=%u\n",
                attempts,
                static_cast<unsigned long long>(feed.total_dropped()),
                static_cast<unsigned long long>(feed.total_messages()),
                consumer.lob().active_order_count());
}

}  // namespace

int main() {
    std::printf("LOB Reconstruction Integration Test\n");

    {
        luv::Arena arena;
        assert(arena.init());
        test_lob_reconstruction(arena);
    }

    test_consumer_and_features();
    test_sim_feed_pipeline();
    test_fault_injected_feed();
    test_aggressive_fault_injected_feed();

    std::printf("\nAll LOB tests passed.\n");
    return 0;
}
