/**
 * packet_io_stub.c
 * 
 * macOS/development stub for packet I/O.
 * Provides a tiny deterministic packet source so the packet_io boundary is
 * testable without requiring a real network stack or DPDK dependency.
 *
 * No DPDK dependency; compiles on any POSIX system.
 */

#include "packet_io.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct sim_packet {
    uint32_t seq;
    uint8_t payload[32];
};

struct sim_feed_source {
    struct sim_packet* packets;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    uint32_t last_seq;
    struct sim_packet* last_packet;
    uint8_t duplicate_next;
};

void* sim_feed_source_create(uint32_t num_messages) {
    if (num_messages == 0U) {
        num_messages = 64U;
    }

    struct sim_feed_source* src = calloc(1U, sizeof(*src));
    if (!src) {
        return NULL;
    }

    src->packets = calloc((size_t)num_messages, sizeof(struct sim_packet));
    if (!src->packets) {
        free(src);
        return NULL;
    }

    src->capacity = num_messages;
    src->count = num_messages;
    src->head = 0U;
    src->last_seq = 0U;
    src->last_packet = NULL;
    src->duplicate_next = 0U;

    for (uint32_t i = 0; i < num_messages; ++i) {
        src->packets[i].seq = i + 1U;
        memset(src->packets[i].payload, 0xA5U, sizeof(src->packets[i].payload));
        src->packets[i].payload[0] = (uint8_t)((i >> 24) & 0xFFU);
        src->packets[i].payload[1] = (uint8_t)((i >> 16) & 0xFFU);
        src->packets[i].payload[2] = (uint8_t)((i >> 8) & 0xFFU);
        src->packets[i].payload[3] = (uint8_t)(i & 0xFFU);
    }

    return src;
}

void sim_feed_source_destroy(void* src) {
    if (!src) {
        return;
    }

    struct sim_feed_source* feed = (struct sim_feed_source*)src;
    free(feed->packets);
    free(feed);
}

void* sim_feed_source_next_packet(void* src) {
    if (!src) {
        return NULL;
    }

    struct sim_feed_source* feed = (struct sim_feed_source*)src;
    if (feed->count == 0U) {
        return NULL;
    }

    if (feed->duplicate_next) {
        feed->duplicate_next = 0U;
        if (feed->last_packet) {
            return feed->last_packet;
        }
    }

    struct sim_packet* pkt = &feed->packets[feed->head];
    feed->head = (feed->head + 1U) % feed->capacity;
    feed->count--;
    feed->last_packet = pkt;
    feed->last_seq = pkt->seq;
    return pkt;
}

void sim_feed_source_inject_gap(void* src, uint16_t gap_size) {
    if (!src || gap_size == 0U) {
        return;
    }

    struct sim_feed_source* feed = (struct sim_feed_source*)src;
    uint32_t skip = (uint32_t)gap_size;
    if (skip > feed->count) {
        skip = feed->count;
    }
    feed->head = (feed->head + skip) % feed->capacity;
    feed->count -= skip;
}

void sim_feed_source_inject_duplicate(void* src) {
    if (!src) {
        return;
    }

    struct sim_feed_source* feed = (struct sim_feed_source*)src;
    feed->duplicate_next = 1U;
}

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
