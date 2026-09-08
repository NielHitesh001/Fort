#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <span>
#include "luv_execution.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

namespace luv {
namespace sharding {

// Lock-free bounded SPSC Ring Buffer for inter-core order dispatch
template <typename T, size_t Capacity = 4096>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    SpscRingBuffer() noexcept : head_(0), tail_(0) {}

    bool push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (head - tail >= Capacity) {
            return false; // Queue is full
        }

        buffer_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out_item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return false; // Queue is empty
        }

        out_item = buffer_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::array<T, Capacity> buffer_{};
};

// Sharded Symbol Router
template <size_t NumShards = 4>
class SymbolShardedDispatcher {
    static_assert((NumShards & (NumShards - 1)) == 0, "NumShards must be a power of two");
public:
    static constexpr size_t kShards = NumShards;

    // Dispatches an order intent to the dedicated symbol shard
    bool dispatch(const exec::OrderIntent& order) noexcept {
        const size_t shard = get_shard_index(order.symbol_idx);
        return ring_buffers_[shard].push(order);
    }

    // Worker polls its assigned shard
    bool poll_shard(size_t shard_idx, exec::OrderIntent& out_order) noexcept {
        if (shard_idx >= NumShards) return false;
        return ring_buffers_[shard_idx].pop(out_order);
    }

    static constexpr size_t get_shard_index(uint16_t symbol_idx) noexcept {
        return symbol_idx & (NumShards - 1);
    }

    // Set CPU core affinity for the current thread
    static bool pin_current_thread_to_core(uint32_t core_id) noexcept {
#if defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#elif defined(__APPLE__)
        thread_affinity_policy_data_t policy = { static_cast<integer_t>(core_id + 1) };
        thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());
        kern_return_t ret = thread_policy_set(
            mach_thread,
            THREAD_AFFINITY_POLICY,
            reinterpret_cast<thread_policy_t>(&policy),
            THREAD_AFFINITY_POLICY_COUNT
        );
        return (ret == KERN_SUCCESS || ret == KERN_NOT_SUPPORTED);
#else
        (void)core_id;
        return true;
#endif
    }

private:
    std::array<SpscRingBuffer<exec::OrderIntent, 1024>, NumShards> ring_buffers_{};
};

} // namespace sharding
} // namespace luv
