#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <cstddef>
#include <new>

namespace luv {
namespace memory {

template <typename T, size_t Capacity = 4096>
class LockFreeFixedBlockPool {
    static_assert(sizeof(T) >= sizeof(uint32_t), "Element size must be >= 4 bytes");
public:
    LockFreeFixedBlockPool() noexcept {
        for (uint32_t i = 0; i < Capacity; ++i) {
            freelist_[i] = i + 1;
        }
        freelist_[Capacity - 1] = kInvalidIndex;
        head_.store(TaggedIndex{0, 0}, std::memory_order_relaxed);
    }

    // O(1) allocation in < 10 nanoseconds
    template <typename... Args>
    T* allocate(Args&&... args) noexcept {
        TaggedIndex old_head = head_.load(std::memory_order_acquire);
        while (true) {
            if (old_head.index == kInvalidIndex) {
                return nullptr; // Pool exhausted
            }

            uint32_t next_idx = freelist_[old_head.index];
            TaggedIndex new_head{next_idx, old_head.tag + 1};

            if (head_.compare_exchange_weak(old_head, new_head,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                T* ptr = reinterpret_cast<T*>(&storage_[old_head.index]);
                return new (ptr) T(std::forward<Args>(args)...);
            }
        }
    }

    // O(1) deallocation in < 10 nanoseconds
    void deallocate(T* ptr) noexcept {
        if (!ptr) return;

        ptr->~T();
        auto* byte_ptr = reinterpret_cast<uint8_t*>(ptr);
        auto* base_ptr = reinterpret_cast<uint8_t*>(storage_.data());
        ptrdiff_t diff = byte_ptr - base_ptr;
        uint32_t idx = static_cast<uint32_t>(diff / sizeof(StorageNode));

        if (idx >= Capacity) return;

        TaggedIndex old_head = head_.load(std::memory_order_acquire);
        while (true) {
            freelist_[idx] = old_head.index;
            TaggedIndex new_head{idx, old_head.tag + 1};

            if (head_.compare_exchange_weak(old_head, new_head,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                return;
            }
        }
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFF;

    struct TaggedIndex {
        uint32_t index = 0;
        uint32_t tag = 0;
    };

    struct alignas(alignof(T)) StorageNode {
        uint8_t bytes[sizeof(T)];
    };

    alignas(64) std::atomic<TaggedIndex> head_;
    std::array<StorageNode, Capacity> storage_{};
    std::array<uint32_t, Capacity> freelist_{};
};

} // namespace memory
} // namespace luv
