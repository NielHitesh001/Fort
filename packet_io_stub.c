/**
 * packet_io_stub.c
 * 
 * macOS/development stub for packet I/O.
 * Reuses SimFeedSource fault-injection machinery for deterministic testing.
 * 
 * No DPDK dependency; compiles on any POSIX system.
 */

#include "packet_io.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations — these come from your existing test harness */
extern void* sim_feed_source_create(uint32_t num_messages);
extern void sim_feed_source_destroy(void* src);
extern void* sim_feed_source_next_packet(void* src);
extern void sim_feed_source_inject_gap(void* src, uint16_t gap_size);
extern void sim_feed_source_inject_duplicate(void* src);

static void* g_feed_source = NULL;

/**
 * packet_io_init_stub
 * 
 * Initialize the stub feed source.
 * num_rx_queues and num_tx_queues are ignored; stub is single-threaded.
 */
static int packet_io_init_stub(uint16_t num_rx_queues, uint16_t num_tx_queues) {
    (void)num_rx_queues;
    (void)num_tx_queues;
    
    /* Create a feed source with default capacity */
    g_feed_source = sim_feed_source_create(250000);
    if (!g_feed_source) {
        fprintf(stderr, "packet_io_init_stub: failed to create feed source\n");
        return -1;
    }
    
    fprintf(stderr, "packet_io_init_stub: initialized with 250k messages\n");
    return 0;
}

/**
 * packet_io_fini_stub
 * 
 * Clean up the feed source.
 */
static void packet_io_fini_stub(void) {
    if (g_feed_source) {
        sim_feed_source_destroy(g_feed_source);
        g_feed_source = NULL;
    }
}

/**
 * packet_io_rx_burst_stub
 * 
 * Fetch up to nb_pkts packets from the feed.
 * Returns number of packets actually fetched (0 if feed exhausted).
 */
static uint16_t packet_io_rx_burst_stub(uint8_t port_id, uint16_t queue_id,
                                         void **packets, uint16_t nb_pkts) {
    (void)port_id;
    (void)queue_id;
    
    if (!g_feed_source || !packets || nb_pkts == 0) {
        return 0;
    }
    
    uint16_t count = 0;
    for (uint16_t i = 0; i < nb_pkts; i++) {
        void* pkt = sim_feed_source_next_packet(g_feed_source);
        if (!pkt) {
            break;  /* Feed exhausted */
        }
        packets[i] = pkt;
        count++;
    }
    
    return count;
}

/**
 * packet_io_tx_burst_stub
 * 
 * No-op transmit (stub ignores packets).
 * In a real implementation, this would validate execution reports.
 */
static uint16_t packet_io_tx_burst_stub(uint8_t port_id, uint16_t queue_id,
                                         void **packets, uint16_t nb_pkts) {
    (void)port_id;
    (void)queue_id;
    (void)packets;
    
    /* Stub accepts all packets but does nothing with them */
    return nb_pkts;
}

/**
 * packet_io_get_next_packet_stub
 * 
 * Direct accessor for tests that bypass rx_burst.
 * Used by test harnesses for fine-grained packet injection.
 */
static void* packet_io_get_next_packet_stub(void) {
    if (!g_feed_source) {
        return NULL;
    }
    return sim_feed_source_next_packet(g_feed_source);
}

/* Global ops table — initialized once */
packet_io_ops_t packet_io_ops = {
    .init             = packet_io_init_stub,
    .fini             = packet_io_fini_stub,
    .rx_burst         = packet_io_rx_burst_stub,
    .tx_burst         = packet_io_tx_burst_stub,
    .get_next_packet  = packet_io_get_next_packet_stub,
};
