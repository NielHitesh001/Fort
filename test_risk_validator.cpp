#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>

#include "luv_execution.hpp"

int main() {
    luv::Arena arena;
    assert(arena.init());

    luv::PreTradeRisk risk;
    assert(risk.init(arena));
    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 2'000;
    limits.max_alpha_age_ns = 1'000'000;
    assert(risk.set_limits(3, limits));

    luv::exec::RiskLimits invalid_limits = limits;
    invalid_limits.max_order_qty = 0;
    assert(!risk.set_limits(3, invalid_limits));
    invalid_limits = limits;
    invalid_limits.max_price = invalid_limits.min_price - 1;
    assert(!risk.set_limits(3, invalid_limits));

    luv::exec::OrderIntent invalid{};
    invalid.symbol_idx = luv::Config::kSymbols;
    assert(risk.evaluate(invalid).pass == 0);

    luv::exec::OrderIntent reversed_timestamp{};
    reversed_timestamp.symbol_idx = 3;
    reversed_timestamp.qty = 1;
    reversed_timestamp.price = 1;
    reversed_timestamp.alpha_timestamp_ns = 2;
    reversed_timestamp.now_ns = 1;
    assert(risk.evaluate(reversed_timestamp).pass == 0);

    std::mt19937_64 rng(0xD1CE);
    for (uint32_t iteration = 0; iteration < 50'000; ++iteration) {
        luv::exec::OrderIntent intent{};
        intent.symbol_idx = 3;
        intent.side = static_cast<uint8_t>(rng() & 1u);
        intent.qty = static_cast<int64_t>(rng() % 5'000);
        intent.price = static_cast<int64_t>(rng() % 2'000'000);
        intent.alpha_timestamp_ns = rng() % 2'000'000;
        intent.now_ns = rng() % 2'000'000;
        const auto result = risk.evaluate(intent);
        if (result.pass) {
            assert(intent.qty > 0);
            assert(intent.price > 0);
        }
    }

    std::printf("risk validator fuzz smoke passed: 50000 inputs\n");
    return 0;
}
