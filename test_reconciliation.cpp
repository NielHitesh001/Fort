#include "luv_reconciliation.hpp"
#include <cassert>
#include <cstdio>

void test_trade_reconciliation_engine() {
    luv::recon::TradeReconciliationEngine recon;

    // 1. Matched trade: FO and BO match exactly
    assert(recon.record_front_office(101, 1, luv::exec::kBuy, 1500000, 100));
    assert(recon.record_back_office(101, 1, luv::exec::kBuy, 1500000, 100));

    // 2. Quantity Break: FO reports 200, BO reports 150
    assert(recon.record_front_office(102, 1, luv::exec::kBuy, 1500000, 200));
    assert(recon.record_back_office(102, 1, luv::exec::kBuy, 1500000, 150));

    // 3. Price Break: FO reports 1500000, BO reports 1501000
    assert(recon.record_front_office(103, 1, luv::exec::kSell, 1500000, 50));
    assert(recon.record_back_office(103, 1, luv::exec::kSell, 1501000, 50));

    // 4. Unmatched FO: Trade executed in FO, dropped in clearing
    assert(recon.record_front_office(104, 2, luv::exec::kBuy, 500000, 300));

    // 5. Unmatched BO: Clearing trade with no matching FO execution
    assert(recon.record_back_office(105, 2, luv::exec::kSell, 500000, 300));

    std::array<luv::recon::ReconBreakReport, 16> breaks{};
    size_t num_breaks = recon.reconcile_trades(breaks.data(), breaks.size());

    // We expect 4 breaks: Order 102 (Qty), Order 103 (Price), Order 104 (Unmatched FO), Order 105 (Unmatched BO)
    assert(num_breaks == 4);

    bool seen_qty = false, seen_price = false, seen_fo_unmatched = false, seen_bo_unmatched = false;
    for (size_t i = 0; i < num_breaks; ++i) {
        if (breaks[i].order_id == 102 && breaks[i].break_type == luv::recon::BreakType::kQuantityBreak) seen_qty = true;
        if (breaks[i].order_id == 103 && breaks[i].break_type == luv::recon::BreakType::kPriceBreak) seen_price = true;
        if (breaks[i].order_id == 104 && breaks[i].break_type == luv::recon::BreakType::kUnmatchedFrontOffice) seen_fo_unmatched = true;
        if (breaks[i].order_id == 105 && breaks[i].break_type == luv::recon::BreakType::kUnmatchedBackOffice) seen_bo_unmatched = true;
    }

    assert(seen_qty);
    assert(seen_price);
    assert(seen_fo_unmatched);
    assert(seen_bo_unmatched);

    std::printf("[PASS] test_trade_reconciliation_engine (Identified %zu breaks successfully)\n", num_breaks);
}

int main() {
    test_trade_reconciliation_engine();
    std::printf("All trade reconciliation tests passed successfully.\n");
    return 0;
}
