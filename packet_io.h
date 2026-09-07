#ifndef PACKET_IO_H
#define PACKET_IO_H

#include <stdint.h>
#include <stddef.h>

/**
 * packet_io_t: Abstract packet I/O interface
 * 
 * Separates DPDK (Linux/CI) from stub (macOS dev) implementations.
 * Both must provide the same deterministic feed contract for testing.
 */

typedef struct packet_io_ops {
    int (*init)(uint16_t num_rx_queues, uint16_t num_tx_queues);
    void (*fini)(void);
    
    /* Receive up to 32 packets from the feed */
    uint16_t (*rx_burst)(uint8_t port_id, uint16_t queue_id,
                         void **packets, uint16_t nb_pkts);
    
    /* Transmit up to 32 packets */
    uint16_t (*tx_burst)(uint8_t port_id, uint16_t queue_id,
                         void **packets, uint16_t nb_pkts);
    
    /* Get next packet from feed (stub-specific, used by tests) */
    void* (*get_next_packet)(void);
    
} packet_io_ops_t;

/* Global ops table — set at startup */
extern packet_io_ops_t packet_io_ops;

/* Convenience wrappers */
static inline int packet_io_init(uint16_t nrx, uint16_t ntx) {
    return packet_io_ops.init(nrx, ntx);
}

static inline void packet_io_fini(void) {
    packet_io_ops.fini();
}

static inline uint16_t packet_io_rx(uint8_t port, uint16_t queue,
                                     void **pkts, uint16_t nb) {
    return packet_io_ops.rx_burst(port, queue, pkts, nb);
}

static inline uint16_t packet_io_tx(uint8_t port, uint16_t queue,
                                     void **pkts, uint16_t nb) {
    return packet_io_ops.tx_burst(port, queue, pkts, nb);
}

#endif // PACKET_IO_H
