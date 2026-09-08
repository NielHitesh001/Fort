#include "luv_sharding.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>

void test_spsc_ring_buffer() {
    luv::sharding::SpscRingBuffer<int, 64> ring;
    assert(ring.empty());
    assert(ring.size() == 0);

    for (int i = 0; i < 64; ++i) {
        bool ok = ring.push(i);
        assert(ok);
    }
    // Full
    assert(!ring.push(999));
    assert(ring.size() == 64);

    for (int i = 0; i < 64; ++i) {
        int val = 0;
        bool ok = ring.pop(val);
        assert(ok);
        assert(val == i);
    }
    assert(ring.empty());
    std::printf("[PASS] test_spsc_ring_buffer\n");
}

void test_symbol_sharding_dispatch() {
    luv::sharding::SymbolShardedDispatcher<4> dispatcher;

    // Core pinning test
    bool pinned = luv::sharding::SymbolShardedDispatcher<4>::pin_current_thread_to_core(0);
    assert(pinned);

    // Dispatch orders across 4 shards
    for (uint16_t sym = 0; sym < 16; ++sym) {
        luv::exec::OrderIntent order;
        order.symbol_idx = sym;
        order.price = 10000 + sym;
        order.qty = 100;
        bool ok = dispatcher.dispatch(order);
        assert(ok);
    }

    // Verify each shard got its respective symbols
    for (size_t shard = 0; shard < 4; ++shard) {
        int count = 0;
        luv::exec::OrderIntent polled;
        while (dispatcher.poll_shard(shard, polled)) {
            assert((polled.symbol_idx % 4) == shard);
            count++;
        }
        assert(count == 4); // 16 symbols / 4 shards = 4 per shard
    }

    std::printf("[PASS] test_symbol_sharding_dispatch\n");
}

void test_concurrent_producer_consumer() {
    luv::sharding::SymbolShardedDispatcher<4> dispatcher;
    constexpr int kOrdersPerShard = 10000;
    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> total_consumed{0};

    // Spawn 4 worker threads (1 per shard)
    std::vector<std::thread> workers;
    for (size_t s = 0; s < 4; ++s) {
        workers.emplace_back([&, s]() {
            luv::sharding::SymbolShardedDispatcher<4>::pin_current_thread_to_core(static_cast<uint32_t>(s));
            luv::exec::OrderIntent order;
            while (true) {
                if (dispatcher.poll_shard(s, order)) {
                    total_consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (producer_done.load(std::memory_order_acquire)) {
                    // Producer is finished, do one final drain check
                    if (dispatcher.poll_shard(s, order)) {
                        total_consumed.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Producer sends orders
    for (int i = 0; i < kOrdersPerShard * 4; ++i) {
        luv::exec::OrderIntent order;
        order.symbol_idx = static_cast<uint16_t>(i % 4);
        order.price = 10000;
        order.qty = 10;
        while (!dispatcher.dispatch(order)) {
            std::this_thread::yield();
        }
    }

    producer_done.store(true, std::memory_order_release);
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    assert(total_consumed.load() == kOrdersPerShard * 4);
    std::printf("[PASS] test_concurrent_producer_consumer (Consumed %llu orders across 4 shards)\n",
        static_cast<unsigned long long>(total_consumed.load()));
}

int main() {
    test_spsc_ring_buffer();
    test_symbol_sharding_dispatch();
    test_concurrent_producer_consumer();
    std::printf("All symbol sharding and lock-free concurrency tests passed successfully.\n");
    return 0;
}
