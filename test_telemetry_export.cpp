#include <cassert>
#include <cstdint>
#include <cstdio>

#include "luv_arena.hpp"
#include "luv_telemetry.hpp"

int main() {
    luv::Arena arena;
    assert(arena.init());

    luv::TelemetryBatchCollector<128> collector;
    for (uint32_t index = 1; index <= 100; ++index)
        collector.record_latency(static_cast<uint64_t>(index) * 1'000);
    for (uint32_t index = 0; index < 10; ++index) {
        collector.record_order_sent();
        collector.record_order_check(true);
        collector.record_ack();
        collector.record_fill(100'000);
    }
    collector.set_position(100);
    assert(collector.flush(arena));

    const luv::TelemSnapshot* snapshot = arena.telem_ring.try_peek();
    assert(snapshot != nullptr);
    assert(snapshot->fill_count == 10);
    assert(snapshot->reject_count == 0);
    assert(snapshot->active_orders == 0);
    assert(snapshot->inference_us == 50.0f);
    assert(snapshot->risk_ns == 99.0f);
    arena.telem_ring.consume();

    luv::TelemetryBatchCollector<1> bounded;
    bounded.record_latency(1);
    bounded.record_latency(2);
    assert(bounded.dropped_latencies() == 1);
    assert(bounded.flush(arena));

    while (arena.telem_ring.try_peek()) arena.telem_ring.consume();
    for (uint32_t index = 0; index < luv::Config::kTelemCapacity; ++index)
        assert(bounded.flush(arena));
    assert(!bounded.flush(arena));
    assert(luv::TelemetryPublisher::dropped(arena) >= 1);

    std::printf("telemetry batch export passed: percentiles and drops verified\n");
    return 0;
}
