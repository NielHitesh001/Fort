#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "luv_strategy.hpp"

using namespace luv;

class MockNormalStrategy : public IStrategy {
public:
    const char* name() const noexcept override { return "MockNormal"; }
    std::vector<exec::OrderIntent> generate_orders(const StrategyConfig& cfg, uint64_t now_ns) override {
        return TWAPStrategy::plan(cfg, now_ns);
    }
};

class MockRogueStrategy : public IStrategy {
public:
    const char* name() const noexcept override { return "MockRogue"; }
    std::vector<exec::OrderIntent> generate_orders(const StrategyConfig&, uint64_t) override {
        // Generates 1,000 orders breaching the max_orders_per_step limit of 10
        std::vector<exec::OrderIntent> orders;
        for (int i = 0; i < 100; ++i) {
            exec::OrderIntent intent{};
            intent.client_order_id = i + 1;
            orders.push_back(intent);
        }
        return orders;
    }
};

class MockCrashingStrategy : public IStrategy {
public:
    const char* name() const noexcept override { return "MockCrashing"; }
    std::vector<exec::OrderIntent> generate_orders(const StrategyConfig&, uint64_t) override {
        throw std::runtime_error("Simulated strategy calculation crash!");
    }
};

void test_strategy_sandboxing() {
    std::printf("[test_strategy_sandboxing] Running...\n");

    StrategySandboxBudget budget{};
    budget.max_orders_per_step = 10;
    budget.max_total_qty = 50'000;

    // 1. Normal execution
    MockNormalStrategy normal_strat;
    StrategySandboxContainer normal_container(&normal_strat, budget);
    assert(normal_container.is_active());

    StrategyConfig cfg{};
    cfg.symbol_idx = 0;
    cfg.side = exec::kBuy;
    cfg.total_qty = 1'000;
    cfg.limit_price = 100 * 10'000;
    cfg.slices = 5;

    auto orders = normal_container.execute_safe(cfg, 1000);
    assert(orders.size() == 5);
    assert(normal_container.is_active());

    // 2. Rogue strategy exceeding order count quota
    MockRogueStrategy rogue_strat;
    StrategySandboxContainer rogue_container(&rogue_strat, budget);
    assert(rogue_container.is_active());

    auto rogue_orders = rogue_container.execute_safe(cfg, 1000);
    assert(rogue_orders.empty());
    assert(rogue_container.state() == StrategyState::kQuarantined);
    assert(rogue_container.violation_count() == 1);
    assert(std::strstr(rogue_container.last_quarantine_reason(), "excessive order count") != nullptr);

    // 3. Strategy throwing unhandled exception
    MockCrashingStrategy crash_strat;
    StrategySandboxContainer crash_container(&crash_strat, budget);
    assert(crash_container.is_active());

    auto crash_orders = crash_container.execute_safe(cfg, 1000);
    assert(crash_orders.empty());
    assert(crash_container.state() == StrategyState::kQuarantined);
    assert(std::strstr(crash_container.last_quarantine_reason(), "Unhandled exception") != nullptr);

    std::printf("[test_strategy_sandboxing] PASSED\n");
}

int main() {
    test_strategy_sandboxing();
    std::printf("ALL STRATEGY SANDBOX TESTS PASSED\n");
    return 0;
}
