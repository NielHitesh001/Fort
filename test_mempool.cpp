#include "luv_mempool.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

struct TestNode {
    uint64_t id = 0;
    int64_t val = 0;
    TestNode(uint64_t i = 0, int64_t v = 0) : id(i), val(v) {}
};

void test_lock_free_pool_alloc_dealloc() {
    luv::memory::LockFreeFixedBlockPool<TestNode, 128> pool;

    std::vector<TestNode*> allocated;
    for (uint64_t i = 0; i < 128; ++i) {
        TestNode* node = pool.allocate(i, static_cast<int64_t>(i * 100));
        assert(node != nullptr);
        assert(node->id == i);
        assert(node->val == static_cast<int64_t>(i * 100));
        allocated.push_back(node);
    }

    // Pool full
    assert(pool.allocate(999, 999) == nullptr);

    // Deallocate all
    for (auto* ptr : allocated) {
        pool.deallocate(ptr);
    }

    // Allocate again (rapid recycling)
    for (uint64_t i = 0; i < 128; ++i) {
        TestNode* node = pool.allocate(i + 1000, static_cast<int64_t>(i));
        assert(node != nullptr);
        assert(node->id == i + 1000);
    }

    std::printf("[PASS] test_lock_free_pool_alloc_dealloc (128 nodes recycled successfully)\n");
}

int main() {
    test_lock_free_pool_alloc_dealloc();
    std::printf("All lock-free memory pool tests passed successfully.\n");
    return 0;
}
