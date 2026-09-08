#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

#include "luv_arena.hpp"
#include "luv_lob.hpp"

namespace {

luv::TickMsg make_tick(uint8_t type, uint16_t sym, uint64_t ref, int64_t qty,
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

uint64_t compute_lob_state_checksum(const luv::LOBEngine& lob, uint16_t sym) {
    uint64_t hash = 14695981039346656037ULL;
    auto mix = [&](int64_t val) {
        hash ^= static_cast<uint64_t>(val);
        hash *= 1099511628211ULL;
    };

    mix(lob.best_bid_price(sym));
    mix(lob.best_ask_price(sym));
    mix(lob.bid_depth_qty(sym, 5));
    mix(lob.ask_depth_qty(sym, 5));
    mix(lob.bid_level_count(sym));
    mix(lob.ask_level_count(sym));

    return hash;
}

std::vector<luv::TickMsg> generate_fuzz_stream(size_t num_ticks, uint64_t seed) {
    std::vector<luv::TickMsg> stream;
    stream.reserve(num_ticks);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int64_t> price_dist(900'000, 1'100'000);
    std::uniform_int_distribution<int64_t> qty_dist(10, 500);
    std::uniform_int_distribution<int> side_dist(0, 1);

    uint64_t ref_id = 1;
    for (size_t i = 0; i < num_ticks; ++i) {
        uint8_t side = side_dist(rng) ? luv::tick_flags::kBuy : 0;
        int64_t px = price_dist(rng);
        int64_t q = qty_dist(rng);
        stream.push_back(make_tick('A', 0, ref_id++, q, px, side));
    }
    return stream;
}

} // namespace

int main() {
    constexpr size_t kFuzzCount = 5000;
    auto stream1 = generate_fuzz_stream(kFuzzCount, 987654321ULL);
    auto stream2 = generate_fuzz_stream(kFuzzCount, 987654321ULL);

    // Run 1
    auto arena1 = std::make_unique<luv::Arena>();
    assert(arena1->init());
    luv::LOBEngine lob1;
    assert(lob1.init(*arena1));

    for (const auto& t : stream1) {
        lob1.process(t);
    }
    uint64_t hash1 = compute_lob_state_checksum(lob1, 0);

    // Run 2
    auto arena2 = std::make_unique<luv::Arena>();
    assert(arena2->init());
    luv::LOBEngine lob2;
    assert(lob2.init(*arena2));

    for (const auto& t : stream2) {
        lob2.process(t);
    }
    uint64_t hash2 = compute_lob_state_checksum(lob2, 0);

    assert(hash1 == hash2);
    assert(lob1.best_bid_price(0) == lob2.best_bid_price(0));
    assert(lob1.best_ask_price(0) == lob2.best_ask_price(0));
    assert(lob1.bid_depth_qty(0, 5) == lob2.bid_depth_qty(0, 5));
    assert(lob1.ask_depth_qty(0, 5) == lob2.ask_depth_qty(0, 5));

    std::printf("Determinism fuzzing passed: 100%% identical state hash (%llu) across %zu ticks.\n",
        static_cast<unsigned long long>(hash1), kFuzzCount);
    return 0;
}
