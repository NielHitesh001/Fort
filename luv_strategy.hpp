#pragma once

#include <cstdint>
#include <vector>

#include "luv_execution.hpp"

namespace luv {

struct StrategyConfig {
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    int64_t total_qty = 0;
    int64_t limit_price = 0;
    uint32_t base_client_order_id = 0;
    uint32_t slices = 1;
    uint64_t interval_ns = 0;
};

class TWAPStrategy {
public:
    static std::vector<exec::OrderIntent> plan(const StrategyConfig& cfg,
                                              uint64_t now_ns) noexcept {
        std::vector<exec::OrderIntent> orders;
        if (cfg.total_qty <= 0 || cfg.slices == 0 || cfg.limit_price <= 0) return orders;

        const int64_t slice_qty = cfg.total_qty / static_cast<int64_t>(cfg.slices);
        const int64_t remainder = cfg.total_qty % static_cast<int64_t>(cfg.slices);
        orders.reserve(cfg.slices);

        for (uint32_t i = 0; i < cfg.slices; ++i) {
            exec::OrderIntent intent{};
            intent.symbol_idx = cfg.symbol_idx;
            intent.side = cfg.side;
            intent.qty = slice_qty + (i == cfg.slices - 1 ? remainder : 0);
            intent.price = cfg.limit_price;
            intent.alpha_timestamp_ns = now_ns;
            intent.now_ns = now_ns + (cfg.interval_ns * i);
            intent.client_order_id = cfg.base_client_order_id + i;
            orders.push_back(intent);
        }
        return orders;
    }
};

class VWAPStrategy {
public:
    static std::vector<exec::OrderIntent> plan(const StrategyConfig& cfg,
                                              uint64_t now_ns) noexcept {
        std::vector<exec::OrderIntent> orders;
        if (cfg.total_qty <= 0 || cfg.slices == 0 || cfg.limit_price <= 0) return orders;

        const int64_t slice_qty = cfg.total_qty / static_cast<int64_t>(cfg.slices);
        const int64_t remainder = cfg.total_qty % static_cast<int64_t>(cfg.slices);
        orders.reserve(cfg.slices);

        for (uint32_t i = 0; i < cfg.slices; ++i) {
            exec::OrderIntent intent{};
            intent.symbol_idx = cfg.symbol_idx;
            intent.side = cfg.side;
            intent.qty = slice_qty + (i == cfg.slices - 1 ? remainder : 0);
            intent.price = cfg.limit_price + static_cast<int64_t>(i) * 100;
            intent.alpha_timestamp_ns = now_ns;
            intent.now_ns = now_ns + (cfg.interval_ns * i);
            intent.client_order_id = cfg.base_client_order_id + i;
            orders.push_back(intent);
        }
        return orders;
    }
};

class IOCStrategy {
public:
    static std::vector<exec::OrderIntent> plan(const StrategyConfig& cfg,
                                              uint64_t now_ns) noexcept {
        std::vector<exec::OrderIntent> orders;
        if (cfg.total_qty <= 0 || cfg.limit_price <= 0) return orders;

        exec::OrderIntent intent{};
        intent.symbol_idx = cfg.symbol_idx;
        intent.side = cfg.side;
        intent.qty = cfg.total_qty;
        intent.price = cfg.limit_price;
        intent.alpha_timestamp_ns = now_ns;
        intent.now_ns = now_ns;
        intent.client_order_id = cfg.base_client_order_id;
        orders.push_back(intent);
        return orders;
    }
};

enum class StrategyState : uint8_t {
    kActive = 0,
    kQuarantined = 1,
    kTerminated = 2,
};

struct StrategySandboxBudget {
    uint64_t max_cpu_cycles_per_step = 100'000; // ~30-50us on modern CPUs
    uint32_t max_orders_per_step = 64;
    int64_t max_total_qty = 1'000'000;
};

class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual const char* name() const noexcept = 0;
    virtual std::vector<exec::OrderIntent> generate_orders(const StrategyConfig& cfg, uint64_t now_ns) = 0;
};

class StrategySandboxContainer {
public:
    explicit StrategySandboxContainer(IStrategy* strategy,
                                     const StrategySandboxBudget& budget = StrategySandboxBudget{}) noexcept
        : _strategy(strategy), _budget(budget), _state(StrategyState::kActive) {}

    [[nodiscard]] StrategyState state() const noexcept { return _state; }
    [[nodiscard]] bool is_active() const noexcept { return _state == StrategyState::kActive; }
    [[nodiscard]] uint64_t violation_count() const noexcept { return _violations; }
    [[nodiscard]] const char* last_quarantine_reason() const noexcept { return _quarantine_reason; }

    void quarantine(const char* reason) noexcept {
        _state = StrategyState::kQuarantined;
        ++_violations;
        if (reason) {
            std::strncpy(_quarantine_reason, reason, sizeof(_quarantine_reason) - 1);
            _quarantine_reason[sizeof(_quarantine_reason) - 1] = '\0';
        }
    }

    void reset() noexcept {
        _state = StrategyState::kActive;
        _quarantine_reason[0] = '\0';
    }

    std::vector<exec::OrderIntent> execute_safe(const StrategyConfig& cfg, uint64_t now_ns) noexcept {
        if (_state != StrategyState::kActive || !_strategy) {
            return {};
        }

        if (cfg.total_qty > _budget.max_total_qty) {
            quarantine("Order size exceeds sandbox budget max_total_qty");
            return {};
        }

        try {
            std::vector<exec::OrderIntent> orders = _strategy->generate_orders(cfg, now_ns);
            if (orders.size() > _budget.max_orders_per_step) {
                quarantine("Strategy generated excessive order count in single step");
                return {};
            }
            return orders;
        } catch (...) {
            quarantine("Unhandled exception caught in strategy execution");
            return {};
        }
    }

private:
    IStrategy* _strategy = nullptr;
    StrategySandboxBudget _budget;
    StrategyState _state = StrategyState::kActive;
    uint64_t _violations = 0;
    char _quarantine_reason[128]{};
};

}  // namespace luv
