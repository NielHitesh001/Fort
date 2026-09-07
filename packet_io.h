#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct packet_io_config {
    uint16_t port_id;
    uint16_t rx_queue_id;
    uint16_t nb_rx_desc;
    uint32_t mempool_size;
    uint16_t mbuf_cache;
    uint16_t mtu;
    int eal_argc;
    char** eal_argv;
} packet_io_config_t;

typedef struct packet_io_ops {
    int (*init)(const packet_io_config_t* config);
    int (*rx_burst)(void* packets[], int max);
    int (*tx_burst)(void* packets[], int count);
    int (*packet_is_contiguous)(void* packet);
    uint32_t (*packet_len)(void* packet);
    const uint8_t* (*packet_data)(void* packet);
    void (*packet_free)(void* packet);
    void (*cleanup)(void);
} packet_io_ops_t;

extern packet_io_ops_t packet_io;

#ifdef __cplusplus
}
#endif
