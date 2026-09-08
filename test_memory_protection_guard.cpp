#include "luv_memory_protection_guard.hpp"
#include <cassert>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

using namespace luv;

void test_secure_memory_zeroization() {
    uint8_t secret_key[32];
    for (size_t i = 0; i < 32; ++i) secret_key[i] = 0xAA;

    MemoryProtectionGuard::secure_zero_memory(secret_key, sizeof(secret_key));
    for (size_t i = 0; i < 32; ++i) {
        assert(secret_key[i] == 0);
    }
}

void test_canary_integrity() {
    uint64_t canary = MemoryProtectionGuard::kCanaryMagic;
    assert(MemoryProtectionGuard::verify_canary(canary) == true);

    canary ^= 0x1;
    assert(MemoryProtectionGuard::verify_canary(canary) == false);
}

void test_page_protection_lock_and_unlock() {
    size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void* buffer = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(buffer != MAP_FAILED);

    uint32_t* data = static_cast<uint32_t*>(buffer);
    data[0] = 12345;

    // Lock page as read-only
    assert(MemoryProtectionGuard::lock_memory_readonly(buffer, page_size) == true);
    assert(data[0] == 12345);

    // Unlock page back to read-write
    assert(MemoryProtectionGuard::unlock_memory_readwrite(buffer, page_size) == true);
    data[0] = 67890;
    assert(data[0] == 67890);

    munmap(buffer, page_size);
}

int main() {
    test_secure_memory_zeroization();
    test_canary_integrity();
    test_page_protection_lock_and_unlock();
    std::cout << "Memory Protection & Security Hardening tests passed.\n";
    return 0;
}
