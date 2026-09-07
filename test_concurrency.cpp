#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "luv_lob.hpp"

namespace {
luv::TickMsg add(uint64_t ref, uint16_t symbol, int64_t quantity) {
    luv::TickMsg tick{};
    tick.msg_type = 'A';
    tick.symbol_idx = symbol;
    tick.order_ref = ref;
    tick.qty = quantity;
    tick.price = 1'000'000 + static_cast<int64_t>(ref % 8) * 10'000;
    tick.flags = luv::tick_flags::kBuy;
    return tick;
}
}

int main() {
    luv::Arena arena;
    assert(arena.init());
    luv::LOBEngine lob;
    assert(lob.init(arena));

    std::atomic<bool> start{false};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (uint64_t ref = 1; ref <= 2'000; ++ref) {
            lob.process(add(ref, 0, 1));
            luv::TickMsg remove{};
            remove.msg_type = 'D';
            remove.symbol_idx = 0;
            remove.order_ref = ref;
            lob.process(remove);
        }
    });
    std::thread reader([&] {
        start.store(true, std::memory_order_release);
        for (uint32_t iteration = 0; iteration < 20'000; ++iteration) {
            (void)lob.best_bid_price(0);
            (void)lob.best_ask_price(0);
            (void)lob.bid_depth_qty(0, 8);
        }
    });

    writer.join();
    reader.join();
    assert(lob.active_order_count() == 0);
    std::printf("LOB concurrent reader/writer smoke passed\n");
    return 0;
}
