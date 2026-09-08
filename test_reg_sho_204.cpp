#include <iostream>
#include <cassert>
#include "luv_reg_sho_204.hpp"

int main() {
    std::cout << "[TEST] Running SEC Rule 204 Reg SHO Close-Out Engine Test...\n";

    luv::RegSho204Engine engine;

    // 1. Register Short Sale FTD on Day 10 (Symbol 101)
    assert(engine.register_ftd(1, 1001, 101, 5000, 10, luv::FtdType::ShortSale));
    // 2. Register Long Sale FTD on Day 10 (Symbol 102)
    assert(engine.register_ftd(2, 1001, 102, 3000, 10, luv::FtdType::LongSale));

    assert(engine.get_open_ftd_count() == 2);
    assert(!engine.is_pre_borrow_required(101));
    assert(!engine.is_pre_borrow_required(102));

    // 3. Advance to Day 11 (S+1): Short sale FTD must be closed out or penalty triggered
    engine.advance_business_day(11);
    const auto* r1 = engine.get_record(1);
    assert(r1 != nullptr);
    assert(r1->status == luv::CloseOutStatus::MandatoryBuyInTriggered);
    assert(engine.is_pre_borrow_required(101)); // Symbol 101 penalty box

    // Long sale FTD (Symbol 102) should still be open on S+1 (has until S+3)
    const auto* r2 = engine.get_record(2);
    assert(r2 != nullptr);
    assert(r2->status == luv::CloseOutStatus::Open);
    assert(!engine.is_pre_borrow_required(102));

    // 4. Cure long sale FTD on Day 12
    assert(engine.cure_ftd(2, 3000));
    assert(engine.get_record(2)->status == luv::CloseOutStatus::Cured);

    // 5. Advance to Day 13 (S+3): Cured FTD 2 should not trigger penalty
    engine.advance_business_day(13);
    assert(!engine.is_pre_borrow_required(102));

    std::cout << "[TEST] SEC Rule 204 Reg SHO Close-Out Engine Test Passed!\n";
    return 0;
}
