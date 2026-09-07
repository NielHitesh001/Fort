#include "packet_io.h"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

static struct rte_mempool* mbuf_pool;
static uint16_t active_port;
static uint16_t active_rx_queue;
static int port_started;

static int dpdk_init(const packet_io_config_t* config) {
    if (!config) return -1;
    if (rte_eal_init(config->eal_argc, config->eal_argv) < 0) return -1;
    if (!rte_eth_dev_is_valid_port(config->port_id)) return -1;

    mbuf_pool = rte_pktmbuf_pool_create(
        "LUV_MBUF_POOL", config->mempool_size, config->mbuf_cache, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) return -1;

    struct rte_eth_conf port_conf = {0};
    port_conf.rxmode.mtu = config->mtu;
    if (rte_eth_dev_configure(config->port_id, 1, 0, &port_conf) < 0) return -1;
    if (rte_eth_rx_queue_setup(config->port_id, config->rx_queue_id,
                               config->nb_rx_desc,
                               rte_eth_dev_socket_id(config->port_id),
                               0, mbuf_pool) < 0) return -1;
    if (rte_eth_dev_start(config->port_id) < 0) return -1;

    active_port = config->port_id;
    active_rx_queue = config->rx_queue_id;
    port_started = 1;
    rte_eth_promiscuous_enable(active_port);
    return 0;
}

static int dpdk_rx_burst(void* packets[], int max) {
    if (!packets || max <= 0) return 0;
    return (int)rte_eth_rx_burst(active_port, active_rx_queue,
                                 (struct rte_mbuf**)packets,
                                 (uint16_t)max);
}

static int dpdk_tx_burst(void* packets[], int count) {
    (void)packets;
    (void)count;
    return 0;
}

static int dpdk_packet_is_contiguous(void* packet) {
    return packet && rte_pktmbuf_is_contiguous((struct rte_mbuf*)packet);
}

static uint32_t dpdk_packet_len(void* packet) {
    return packet ? rte_pktmbuf_pkt_len((struct rte_mbuf*)packet) : 0;
}

static const uint8_t* dpdk_packet_data(void* packet) {
    return packet ? rte_pktmbuf_mtod((struct rte_mbuf*)packet, const uint8_t*) : 0;
}

static void dpdk_packet_free(void* packet) {
    if (packet) rte_pktmbuf_free((struct rte_mbuf*)packet);
}

static void dpdk_cleanup(void) {
    if (port_started) {
        rte_eth_dev_stop(active_port);
        rte_eth_dev_close(active_port);
        port_started = 0;
    }
    mbuf_pool = 0;
}

packet_io_ops_t packet_io = {
    dpdk_init,
    dpdk_rx_burst,
    dpdk_tx_burst,
    dpdk_packet_is_contiguous,
    dpdk_packet_len,
    dpdk_packet_data,
    dpdk_packet_free,
    dpdk_cleanup,
};
