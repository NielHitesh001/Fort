#include "luv_net_ring.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

void test_packet_ring_bursts() {
    luv::net::ZeroCopyPacketRing<64> ring;
    assert(ring.is_empty());
    assert(ring.occupancy() == 0);

    std::array<luv::net::PacketDescriptor, 32> tx_batch{};
    for (uint32_t i = 0; i < 32; ++i) {
        tx_batch[i].length = 100 + i;
        tx_batch[i].rx_timestamp_ns = 1000 + i;
    }

    // Enqueue 32
    size_t written = ring.burst_enqueue(tx_batch.data(), tx_batch.size());
    assert(written == 32);
    assert(ring.occupancy() == 32);

    // Enqueue another 32 -> Full
    written = ring.burst_enqueue(tx_batch.data(), tx_batch.size());
    assert(written == 32);
    assert(ring.is_full());

    // Try to enqueue onto full ring -> 0 written
    written = ring.burst_enqueue(tx_batch.data(), 10);
    assert(written == 0);

    // Dequeue 64
    std::array<luv::net::PacketDescriptor, 64> rx_batch{};
    size_t read = ring.burst_dequeue(rx_batch.data(), rx_batch.size());
    assert(read == 64);
    assert(ring.is_empty());

    assert(rx_batch[0].length == 100);
    assert(rx_batch[31].length == 131);
    assert(rx_batch[32].length == 100);

    std::printf("[PASS] test_packet_ring_bursts\n");
}

int main() {
    test_packet_ring_bursts();
    std::printf("All zero-copy packet descriptor ring tests passed successfully.\n");
    return 0;
}
