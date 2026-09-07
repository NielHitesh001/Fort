#pragma once

// DPDK feed source. Packet acquisition and mbuf ownership are isolated behind
// packet_io so this header is buildable without DPDK headers.

#include "luv_feed.hpp"
#include "luv_decode_itch.hpp"
#include "luv_feed_sim.hpp"
#include "luv_moldudp64.hpp"
#include "luv_safety.hpp"
#include "packet_io.h"

#include <cstdint>
#include <cstring>

namespace luv {

struct DpdkConfig {
    uint16_t port_id = 0;
    uint16_t rx_queue_id = 0;
    uint16_t burst_size = 32;
    uint16_t nb_rx_desc = 1024;
    uint32_t mempool_size = 8191;
    uint16_t mbuf_cache = 256;
    uint16_t mtu = 1500;
    int eal_argc = 0;
    char** eal_argv = nullptr;
    uint32_t payload_offset = 42;
};

class DpdkFeedSource final : public IFeedSource {
public:
    DpdkFeedSource() = default;
    explicit DpdkFeedSource(const DpdkConfig& cfg) noexcept : _cfg(cfg) {}
    ~DpdkFeedSource() override { shutdown(); }

    DpdkFeedSource(const DpdkFeedSource&) = delete;
    DpdkFeedSource& operator=(const DpdkFeedSource&) = delete;

    [[nodiscard]] SymbolTable& symbols() noexcept { return _symbols; }

    [[nodiscard]] bool init(Arena& arena) noexcept override {
        if (!arena.is_initialised()) return false;
        _arena = &arena;
        packet_io_config_t config{};
        config.port_id = _cfg.port_id;
        config.rx_queue_id = _cfg.rx_queue_id;
        config.nb_rx_desc = _cfg.nb_rx_desc;
        config.mempool_size = _cfg.mempool_size;
        config.mbuf_cache = _cfg.mbuf_cache;
        config.mtu = _cfg.mtu;
        config.eal_argc = _cfg.eal_argc;
        config.eal_argv = _cfg.eal_argv;
        if (packet_io.init(&config) != 0) {
            _using_simulation = true;
            return _simulation.init(arena);
        }
        _initialised = true;
        return true;
    }

    [[nodiscard]] uint32_t poll() noexcept override {
        if (_using_simulation) return _simulation.poll();
        if (!_initialised) return 0;
        void* packets[64]{};
        const int requested = _cfg.burst_size > 64 ? 64 : _cfg.burst_size;
        const int received = packet_io.rx_burst(packets, requested);
        if (received <= 0) return 0;

        uint32_t decoded = 0;
        for (int i = 0; i < received; ++i) {
            if (packets[i]) decoded += process_packet(packets[i]);
            packet_io.packet_free(packets[i]);
        }
        return decoded;
    }

    [[nodiscard]] uint64_t total_messages() const noexcept override {
        if (_using_simulation) return _simulation.total_messages();
        return _total_msgs;
    }

    [[nodiscard]] uint64_t total_bytes() const noexcept override {
        if (_using_simulation) return _simulation.total_bytes();
        return _total_bytes;
    }

    [[nodiscard]] bool sequence_healthy() const noexcept {
        return _sequence_breaker.allow();
    }

    [[nodiscard]] uint64_t sequence_gaps() const noexcept {
        return _sequence.gaps();
    }

private:
    static constexpr uint16_t kMaxBurst = 64;

    DpdkConfig _cfg{};
    Arena* _arena = nullptr;
    SymbolTable _symbols{};
    bool _initialised = false;
    bool _using_simulation = false;
    SimFeedSource _simulation{};
    uint64_t _total_msgs = 0;
    uint64_t _total_bytes = 0;
    SequenceTracker _sequence{};
    CircuitBreaker _sequence_breaker{1};

    uint32_t process_packet(void* packet) noexcept {
        if (!packet_io.packet_is_contiguous(packet)) return 0;
        const uint32_t packet_len = packet_io.packet_len(packet);
        if (!_sequence_breaker.allow() ||
            packet_len < _cfg.payload_offset + 20) return 0;

        const uint8_t* data = packet_io.packet_data(packet);
        if (!data) return 0;
        const uint8_t* payload = data + _cfg.payload_offset;
        const uint32_t payload_len = packet_len - _cfg.payload_offset;
        _total_bytes += packet_len;

        moldudp64::Header header{};
        if (!moldudp64::parse_header(payload, payload_len, header) ||
            !moldudp64::validate_message_block(payload, payload_len,
                                               header.message_count)) {
            return 0;
        }

        const SequenceResult result = _sequence.observe(header.sequence);
        if (result == SequenceResult::kGap ||
            result == SequenceResult::kOutOfOrder ||
            result == SequenceResult::kDuplicate) {
            _sequence_breaker.trip();
            return 0;
        }

        moldudp64::MessageIterator messages(payload, payload_len);
        uint32_t decoded = 0;
        const uint8_t* raw_message = nullptr;
        uint16_t message_len = 0;
        while (messages.next(raw_message, message_len)) {
            TickMsg* slot = _arena->tick_ring.try_claim();
            if (!slot) break;
            if (decode_itch(raw_message, message_len, _symbols, *slot)) {
                _arena->tick_ring.commit();
                ++_total_msgs;
                ++decoded;
            }
        }
        return decoded;
    }

    void shutdown() noexcept {
        if (_using_simulation) {
            _using_simulation = false;
            return;
        }
        if (_initialised) {
            packet_io.cleanup();
            _initialised = false;
        }
    }
};

}  // namespace luv
