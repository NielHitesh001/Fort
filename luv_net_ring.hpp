#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <cstring>

namespace luv {
namespace net {

struct alignas(64) PacketDescriptor {
    uint8_t* data = nullptr;
    uint32_t length = 0;
    uint32_t capacity = 2048;
    uint64_t rx_timestamp_ns = 0;
    uint16_t queue_id = 0;
    uint8_t port_id = 0;
    uint8_t flags = 0;
};

template <size_t RingSize = 2048>
class ZeroCopyPacketRing {
    static_assert((RingSize & (RingSize - 1)) == 0, "RingSize must be a power of two");
public:
    ZeroCopyPacketRing() noexcept : head_(0), tail_(0) {}

    // Enqueues a batch of packet descriptors (Tx)
    size_t burst_enqueue(const PacketDescriptor* descs, size_t count) noexcept {
        if (!descs || count == 0) return 0;

        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t free_slots = RingSize - (head - tail);
        const size_t to_write = (count < free_slots) ? count : free_slots;

        for (size_t i = 0; i < to_write; ++i) {
            ring_[(head + i) & (RingSize - 1)] = descs[i];
        }

        head_.store(head + to_write, std::memory_order_release);
        return to_write;
    }

    // Dequeues a batch of packet descriptors (Rx)
    size_t burst_dequeue(PacketDescriptor* out_descs, size_t max_count) noexcept {
        if (!out_descs || max_count == 0) return 0;

        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t available = head - tail;
        const size_t to_read = (max_count < available) ? max_count : available;

        for (size_t i = 0; i < to_read; ++i) {
            out_descs[i] = ring_[(tail + i) & (RingSize - 1)];
        }

        tail_.store(tail + to_read, std::memory_order_release);
        return to_read;
    }

    size_t occupancy() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    bool is_full() const noexcept {
        return occupancy() >= RingSize;
    }

    bool is_empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::array<PacketDescriptor, RingSize> ring_{};
};

} // namespace net
} // namespace luv
