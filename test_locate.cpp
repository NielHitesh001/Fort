#include "luv_locate.hpp"
#include <cassert>
#include <cstdio>

void test_short_sale_locate_lifecycle() {
    luv::reg_sho::ShortSaleLocateEngine engine;
    uint16_t sym = 1;
    uint32_t client_1 = 1001;

    // Register borrow pool: 10,000 shares of AAPL available
    assert(engine.set_borrow_inventory(sym, 10000, 75.0, false));

    // Request Locate 1: 3,000 shares
    luv::reg_sho::LocateAllocation alloc1;
    const char* err = nullptr;
    assert(engine.request_locate(1, client_1, sym, 3000, 1000, alloc1, &err));
    assert(alloc1.shares == 3000);
    assert(alloc1.borrow_fee_bps == 75.0);
    assert(alloc1.active);

    // Validate short order: 2,500 shares -> Valid (< 3000 located)
    assert(engine.validate_short_order(client_1, sym, 2500, 2000));

    // Validate short order: 4,000 shares -> Invalid (> 3000 located)
    assert(!engine.validate_short_order(client_1, sym, 4000, 2000));

    // Request Locate 2: 8,000 shares -> Denied (only 7,000 shares left)
    luv::reg_sho::LocateAllocation alloc2;
    assert(!engine.request_locate(2, client_1, sym, 8000, 1000, alloc2, &err));

    // Request Locate 3: 7,000 shares -> Granted (Pool fully allocated)
    assert(engine.request_locate(3, client_1, sym, 7000, 1000, alloc2, &err));
    assert(alloc2.shares == 7000);

    // Total located for client_1 = 3,000 + 7,000 = 10,000
    assert(engine.validate_short_order(client_1, sym, 10000, 2000));

    std::printf("[PASS] test_short_sale_locate_lifecycle\n");
}

int main() {
    test_short_sale_locate_lifecycle();
    std::printf("All SEC Reg SHO locate tests passed successfully.\n");
    return 0;
}
