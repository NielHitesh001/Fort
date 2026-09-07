#include "packet_io.h"

static int stub_init(const packet_io_config_t* config) {
    (void)config;
    return -1;
}

static int stub_rx_burst(void* packets[], int max) {
    (void)packets;
    (void)max;
    return 0;
}

static int stub_tx_burst(void* packets[], int count) {
    (void)packets;
    (void)count;
    return 0;
}

static int stub_packet_is_contiguous(void* packet) {
    (void)packet;
    return 0;
}

static uint32_t stub_packet_len(void* packet) {
    (void)packet;
    return 0;
}

static const uint8_t* stub_packet_data(void* packet) {
    (void)packet;
    return 0;
}

static void stub_packet_free(void* packet) {
    (void)packet;
}

static void stub_cleanup(void) {}

packet_io_ops_t packet_io = {
    stub_init,
    stub_rx_burst,
    stub_tx_burst,
    stub_packet_is_contiguous,
    stub_packet_len,
    stub_packet_data,
    stub_packet_free,
    stub_cleanup,
};
