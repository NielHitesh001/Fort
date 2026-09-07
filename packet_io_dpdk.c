/**
 * packet_io_dpdk.c
 * 
 * Linux/CI DPDK packet I/O implementation.
 * Real hardware or vfio-user backend.
 * 
 * Only compiled when LUV_ENABLE_DPDK=ON on Linux.
 */

#include "packet_io.h"
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <stdio.h>
#include <stdlib.h>

#define MEMPOOL_CACHE_SZ 256
#define RX_RING_SZ 1024
#define TX_RING_SZ 1024
#define NUM_MBUFS 8192

static uint16_t g_port_id = 0;
static struct rte_mempool* g_mbuf_pool = NULL;

/**
 * packet_io_init_dpdk
 * 
 * Initialize DPDK EAL and ethdev.
 * Assumes DPDK EAL already initialized by main(); this just sets up the port.
 */
static int packet_io_init_dpdk(uint16_t num_rx_queues, uint16_t num_tx_queues) {
    int ret;
    
    /* Configure port */
    struct rte_eth_conf port_conf = {
        .rxmode = { .mq_mode = RTE_ETH_MQ_RX_RSS },
        .txmode = { .mq_mode = RTE_ETH_MQ_TX_NONE },
    };
    
    /* Create mbuf pool */
    g_mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NUM_MBUFS,
                                           MEMPOOL_CACHE_SZ, 0,
                                           RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!g_mbuf_pool) {
        fprintf(stderr, "packet_io_init_dpdk: failed to create mbuf pool\n");
        return -1;
    }
    
    /* Configure device */
    ret = rte_eth_dev_configure(g_port_id, num_rx_queues, num_tx_queues, &port_conf);
    if (ret < 0) {
        fprintf(stderr, "packet_io_init_dpdk: failed to configure device: %d\n", ret);
        return -1;
    }
    
    /* Setup RX queues */
    for (uint16_t i = 0; i < num_rx_queues; i++) {
        ret = rte_eth_rx_queue_setup(g_port_id, i, RX_RING_SZ,
                                      rte_eth_dev_socket_id(g_port_id), NULL, g_mbuf_pool);
        if (ret < 0) {
            fprintf(stderr, "packet_io_init_dpdk: failed to setup RX queue %u: %d\n", i, ret);
            return -1;
        }
    }
    
    /* Setup TX queues */
    for (uint16_t i = 0; i < num_tx_queues; i++) {
        ret = rte_eth_tx_queue_setup(g_port_id, i, TX_RING_SZ,
                                      rte_eth_dev_socket_id(g_port_id), NULL);
        if (ret < 0) {
            fprintf(stderr, "packet_io_init_dpdk: failed to setup TX queue %u: %d\n", i, ret);
            return -1;
        }
    }
    
    /* Start device */
    ret = rte_eth_dev_start(g_port_id);
    if (ret < 0) {
        fprintf(stderr, "packet_io_init_dpdk: failed to start device: %d\n", ret);
        return -1;
    }
    
    fprintf(stderr, "packet_io_init_dpdk: initialized port %u with %u RX, %u TX queues\n",
            g_port_id, num_rx_queues, num_tx_queues);
    return 0;
}

/**
 * packet_io_fini_dpdk
 * 
 * Stop device and clean up.
 */
static void packet_io_fini_dpdk(void) {
    if (rte_eth_dev_is_valid_port(g_port_id)) {
        rte_eth_dev_stop(g_port_id);
        rte_eth_dev_close(g_port_id);
    }
    
    if (g_mbuf_pool) {
        rte_mempool_free(g_mbuf_pool);
        g_mbuf_pool = NULL;
    }
}

/**
 * packet_io_rx_burst_dpdk
 * 
 * Native DPDK rx_burst call.
 */
static uint16_t packet_io_rx_burst_dpdk(uint8_t port_id, uint16_t queue_id,
                                         void **packets, uint16_t nb_pkts) {
    return rte_eth_rx_burst(port_id, queue_id, (struct rte_mbuf**)packets, nb_pkts);
}

/**
 * packet_io_tx_burst_dpdk
 * 
 * Native DPDK tx_burst call.
 */
static uint16_t packet_io_tx_burst_dpdk(uint8_t port_id, uint16_t queue_id,
                                         void **packets, uint16_t nb_pkts) {
    return rte_eth_tx_burst(port_id, queue_id, (struct rte_mbuf**)packets, nb_pkts);
}

/**
 * packet_io_get_next_packet_dpdk
 * 
 * Not used in DPDK path; burst API is preferred.
 * Stub returns NULL.
 */
static void* packet_io_get_next_packet_dpdk(void) {
    return NULL;
}

/* Global ops table */
packet_io_ops_t packet_io_ops = {
    .init             = packet_io_init_dpdk,
    .fini             = packet_io_fini_dpdk,
    .rx_burst         = packet_io_rx_burst_dpdk,
    .tx_burst         = packet_io_tx_burst_dpdk,
    .get_next_packet  = packet_io_get_next_packet_dpdk,
};
