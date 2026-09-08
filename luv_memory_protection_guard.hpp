#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace luv {

class MemoryProtectionGuard {
public:
    static constexpr uint64_t kCanaryMagic = 0xDEADC0DECAFEF00DULL;

    // Securely wipe sensitive memory buffers without compiler dead-code elimination
    static void secure_zero_memory(void* ptr, size_t len) noexcept {
        if (!ptr || len == 0) return;
        volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
        while (len--) {
            *p++ = 0;
        }
    }

    // Protect memory region as Read-Only (PROT_READ) to prevent in-memory tampering
    static bool lock_memory_readonly(void* addr, size_t len) noexcept {
        if (!addr || len == 0) return false;
        uintptr_t page_size = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
        uintptr_t page_start = reinterpret_cast<uintptr_t>(addr) & ~(page_size - 1);
        size_t aligned_len = (reinterpret_cast<uintptr_t>(addr) + len - page_start + page_size - 1) & ~(page_size - 1);

        return (mprotect(reinterpret_cast<void*>(page_start), aligned_len, PROT_READ) == 0);
    }

    // Unlock memory region as Read-Write (PROT_READ | PROT_WRITE)
    static bool unlock_memory_readwrite(void* addr, size_t len) noexcept {
        if (!addr || len == 0) return false;
        uintptr_t page_size = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
        uintptr_t page_start = reinterpret_cast<uintptr_t>(addr) & ~(page_size - 1);
        size_t aligned_len = (reinterpret_cast<uintptr_t>(addr) + len - page_start + page_size - 1) & ~(page_size - 1);

        return (mprotect(reinterpret_cast<void*>(page_start), aligned_len, PROT_READ | PROT_WRITE) == 0);
    }

    // Canary boundary check to verify buffer integrity
    static bool verify_canary(uint64_t canary_value) noexcept {
        return (canary_value == kCanaryMagic);
    }
};

} // namespace luv
