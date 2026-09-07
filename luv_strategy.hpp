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

}  // namespace luv
