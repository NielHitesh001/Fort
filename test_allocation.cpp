#include "luv_allocation.hpp"
#include <cassert>
#include <cstdio>

void test_trade_allocation_pro_rata() {
    luv::booking::TradeAllocationEngine allocator;

    // Allocate across 3 sub-accounts:
    // Sub-Account 1: 50%
    // Sub-Account 2: 30%
    // Sub-Account 3: 20%
    assert(allocator.register_target(101, 0.50));
    assert(allocator.register_target(102, 0.30));
    assert(allocator.register_target(103, 0.20));

    // Parent block fill: 1,000 shares @ $150.00 (1500000)
    std::array<luv::booking::AllocatedTradeRecord, 8> out_alloc{};
    size_t count = allocator.allocate_block(1000, 1500000, out_alloc.data(), out_alloc.size());

    assert(count == 3);
    assert(out_alloc[0].sub_account_id == 101 && out_alloc[0].allocated_qty == 500);
    assert(out_alloc[1].sub_account_id == 102 && out_alloc[1].allocated_qty == 300);
    assert(out_alloc[2].sub_account_id == 103 && out_alloc[2].allocated_qty == 200);

    // Test fractional odd lot rounding: 101 shares
    // 50% = 50, 30% = 30, remaining = 21 -> sum = 101 exactly
    count = allocator.allocate_block(101, 1500000, out_alloc.data(), out_alloc.size());
    assert(count == 3);
    assert(out_alloc[0].allocated_qty + out_alloc[1].allocated_qty + out_alloc[2].allocated_qty == 101);

    std::printf("[PASS] test_trade_allocation_pro_rata (Allocated 1000 shares -> 500/300/200; 101 shares -> %lld/%lld/%lld)\n",
        static_cast<long long>(out_alloc[0].allocated_qty),
        static_cast<long long>(out_alloc[1].allocated_qty),
        static_cast<long long>(out_alloc[2].allocated_qty));
}

int main() {
    test_trade_allocation_pro_rata();
    std::printf("All trade allocation tests passed successfully.\n");
    return 0;
}
