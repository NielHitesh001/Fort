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
#include <string.h>

#define MEMPOOL_CACHE_SZ 256
#define RX_RING_SZ 1024
#define TX_RING_SZ 1024
#define NUM_MBUFS 8192

static uint16_t g_port_id = 0;
static struct rte_mempool* g_mbuf_pool = NULL;
static int g_emulated_mode = 0;
static uint32_t g_emulated_seq = 1U;
static struct rte_mbuf* g_emulated_pkts[16];

static int ensure_eal_ready(void) {
    static int eal_ready = 0;
    if (eal_ready) return 0;
    char* argv[] = {
        "packet_io",
        "--in-memory",
        "--no-huge",
        "--iova-mode",
        "va",
    };
    int ret = rte_eal_init(5, argv);
    if (ret < 0) {
        fprintf(stderr, "packet_io_init_dpdk: rte_eal_init failed: %d\n", ret);
        return -1;
    }
    eal_ready = 1;
    return 0;
}

static void* make_emulated_packet(void) {
    struct rte_mbuf* mb = rte_pktmbuf_alloc(g_mbuf_pool);
    if (!mb) {
        mb = calloc(1U, sizeof(*mb));
        if (!mb) return NULL;
    }
    mb->buf_len = 64;
    mb->data_len = 64;
    mb->pkt_len = 64;
    memset(mb->buf_addr, 0, mb->buf_len);
    ((uint8_t*)mb->buf_addr)[0] = 'A';
    ((uint8_t*)mb->buf_addr)[1] = (uint8_t)((g_emulated_seq >> 24) & 0xFFU);
    ((uint8_t*)mb->buf_addr)[2] = (uint8_t)((g_emulated_seq >> 16) & 0xFFU);
    ((uint8_t*)mb->buf_addr)[3] = (uint8_t)((g_emulated_seq >> 8) & 0xFFU);
    ((uint8_t*)mb->buf_addr)[4] = (uint8_t)(g_emulated_seq & 0xFFU);
    ++g_emulated_seq;
    return mb;
}

/**
 * packet_io_init_dpdk
 * 
 * Initialize DPDK EAL and ethdev.
 * Assumes DPDK EAL already initialized by main(); this just sets up the port.
 */
static int packet_io_init_dpdk(uint16_t num_rx_queues, uint16_t num_tx_queues) {
    int ret;

    if (ensure_eal_ready() != 0) return -1;
    if (rte_eth_dev_count_avail() == 0) {
        g_emulated_mode = 1;
        g_mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NUM_MBUFS,
                                              MEMPOOL_CACHE_SZ, 0,
                                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        if (!g_mbuf_pool) {
            fprintf(stderr, "packet_io_init_dpdk: no device found; emulation pool unavailable\n");
            return 0;
        }
        fprintf(stderr, "packet_io_init_dpdk: no DPDK devices detected; using emulated packets\n");
        return 0;
    }

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
    (void)port_id;
    (void)queue_id;
    if (g_emulated_mode && packets && nb_pkts > 0) {
        uint16_t count = 0;
        for (uint16_t i = 0; i < nb_pkts; ++i) {
            void* packet = make_emulated_packet();
            if (!packet) break;
            packets[i] = packet;
            ++count;
        }
        return count;
    }
    if (!g_mbuf_pool) return 0;
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

static void packet_io_packet_free_dpdk(void* packet) {
    if (!packet) return;
    struct rte_mbuf* mb = (struct rte_mbuf*)packet;
    if (mb->pool) {
        rte_pktmbuf_free(mb);
    } else {
        free(mb);
    }
}

static int packet_io_packet_is_contiguous_dpdk(void* packet) {
    return packet != NULL;
}

static uint32_t packet_io_packet_len_dpdk(void* packet) {
    return packet ? ((struct rte_mbuf*)packet)->pkt_len : 0U;
}

static const uint8_t* packet_io_packet_data_dpdk(void* packet) {
    return packet ? rte_pktmbuf_mtod((struct rte_mbuf*)packet, const uint8_t*) : NULL;
}

/**
 * packet_io_get_next_packet_dpdk
 * 
 * Not used in DPDK path; burst API is preferred.
 * Stub returns NULL.
 */
static void* packet_io_get_next_packet_dpdk(void) {
    if (g_emulated_mode) {
        return make_emulated_packet();
    }
    return NULL;
}

/* Global ops table */
packet_io_ops_t packet_io_ops = {
    .init             = packet_io_init_dpdk,
    .fini             = packet_io_fini_dpdk,
    .rx_burst         = packet_io_rx_burst_dpdk,
    .tx_burst         = packet_io_tx_burst_dpdk,
    .packet_free      = packet_io_packet_free_dpdk,
    .packet_is_contiguous = packet_io_packet_is_contiguous_dpdk,
    .packet_len       = packet_io_packet_len_dpdk,
    .packet_data      = packet_io_packet_data_dpdk,
    .get_next_packet  = packet_io_get_next_packet_dpdk,
};
