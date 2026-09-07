#include "packet_io.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void* sim_feed_source_create(uint32_t num_messages);
void sim_feed_source_destroy(void* src);
void* sim_feed_source_next_packet(void* src);
void sim_feed_source_inject_gap(void* src, uint16_t gap_size);
void sim_feed_source_inject_duplicate(void* src);
}

static void test_packet_io_stub_contract() {
    printf("\n══ packet_io stub contract ═══════════════════════\n");

    int rc = packet_io_init(1, 1);
    assert(rc == 0);

    void* packets[8] = {};
    const uint16_t rx = packet_io_rx(0, 0, packets, 8);
    assert(rx >= 1);
    assert(packets[0] != nullptr);

    void* pkt = packet_io_ops.get_next_packet();
    assert(pkt != nullptr);

    packet_io_fini();
    printf("  [OK] stub init/rx/get_next_packet/fini contract holds\n");
}

int main() {
    test_packet_io_stub_contract();
    return 0;
}
