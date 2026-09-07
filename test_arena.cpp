#include <iostream>
#include <cassert>
#include "luv_arena.hpp"

int main() {
    luv::Arena arena;
    if (!arena.init()) {
        std::cerr << "Arena initialization failed" << std::endl;
        return 1;
    }
    auto report = arena.report();
    std::cout << "Memory Usage Report:" << std::endl;
    std::cout << "LOB bytes: " << report.lob_bytes << std::endl;
    std::cout << "Tick ring bytes: " << report.tick_ring_bytes << std::endl;
    std::cout << "Feature bytes: " << report.feature_bytes << std::endl;
    std::cout << "Signal bytes: " << report.signal_bytes << std::endl;
    std::cout << "Exec bytes: " << report.exec_bytes << std::endl;
    std::cout << "Telemetry bytes: " << report.telem_bytes << std::endl;
    std::cout << "Total infrastructure bytes (rounded): " << report.infra_total_bytes << std::endl;
    std::cout << "AI region bytes: " << report.ai_region_bytes << std::endl;
    std::cout << "Grand total bytes: " << report.grand_total_bytes << std::endl;
    std::cout << "mlocked: " << (report.mlocked ? "yes" : "no") << std::endl;

    // Test ShardedTickRing initialization and routing
    luv::ShardedTickRing<4, (1u << 20)> sharded_ring;
    assert(sharded_ring.init(arena.tick_slots.data()));
    assert(sharded_ring.is_initialised());
    assert(sharded_ring.shard_count() == 4);
    assert(sharded_ring.total_capacity() == (1ULL << 22));

    auto& shard0 = sharded_ring.shard_for_symbol(0);
    auto& shard1 = sharded_ring.shard_for_symbol(1);
    auto& shard4 = sharded_ring.shard_for_symbol(4);

    assert(&shard0 == &shard4); // symbol 0 and symbol 4 map to same shard (0 % 4 == 4 % 4 == 0)
    assert(&shard0 != &shard1);

    luv::TickMsg* msg = shard0.try_claim();
    assert(msg != nullptr);
    msg->symbol_idx = 0;
    msg->price = 12345;
    shard0.commit();

    assert(shard0.size() == 1);
    assert(shard1.size() == 0);

    luv::TickMsg* peeked = shard0.try_peek();
    assert(peeked != nullptr);
    assert(peeked->symbol_idx == 0);
    assert(peeked->price == 12345);
    shard0.consume();
    assert(shard0.size() == 0);

    std::cout << "ShardedTickRing multi-asset sharding tests passed." << std::endl;
    return 0;
}
