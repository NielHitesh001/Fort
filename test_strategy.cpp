#include <cassert>
#include <cstdio>
#include <vector>

#include "luv_strategy.hpp"

int main() {
    using namespace luv;

    StrategyConfig cfg{};
    cfg.symbol_idx = 0;
    cfg.side = exec::kBuy;
    cfg.total_qty = 1000;
    cfg.limit_price = 1'000'000;
    cfg.base_client_order_id = 42;
    cfg.slices = 5;
    cfg.interval_ns = 250'000;

    const std::vector<exec::OrderIntent> twap = TWAPStrategy::plan(cfg, 1'000'000);
    assert(!twap.empty());
    assert(twap.size() == 5u);
    int64_t total = 0;
    for (const auto& order : twap) {
        total += order.qty;
        assert(order.symbol_idx == cfg.symbol_idx);
        assert(order.side == cfg.side);
        assert(order.price == cfg.limit_price);
    }
    assert(total == cfg.total_qty);

    const std::vector<exec::OrderIntent> ioc = IOCStrategy::plan(cfg, 1'000'000);
    assert(ioc.size() == 1u);
    assert(ioc[0].qty == cfg.total_qty);

    const std::vector<exec::OrderIntent> vwap = VWAPStrategy::plan(cfg, 1'000'000);
    assert(!vwap.empty());
    assert(vwap.size() == 5u);
    total = 0;
    for (const auto& order : vwap) total += order.qty;
    assert(total == cfg.total_qty);

    std::printf("[OK] strategy planner\n");
    return 0;
}
