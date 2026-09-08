#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include "luv_execution.hpp"

namespace luv {
namespace mbo {

struct MboOrder {
    uint64_t order_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    int64_t price = 0;
    int64_t qty = 0;
    uint64_t priority_ts = 0;
    bool active = false;
};

class Level3MarketByOrderBook {
public:
    static constexpr size_t kMaxOrders = 4096;

    Level3MarketByOrderBook() noexcept : order_count_(0) {
        for (auto& o : orders_) o.active = false;
    }

    bool add_order(uint64_t order_id, uint16_t symbol, uint8_t side, int64_t price, int64_t qty, uint64_t ts) noexcept {
        if (order_count_ >= kMaxOrders || qty <= 0) return false;

        // Check if order_id already exists
        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (orders_[i].active && orders_[i].order_id == order_id) {
                return false; // Duplicate order_id
            }
        }

        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (!orders_[i].active) {
                orders_[i] = MboOrder{
                    .order_id = order_id,
                    .symbol_idx = symbol,
                    .side = side,
                    .price = price,
                    .qty = qty,
                    .priority_ts = ts,
                    .active = true
                };
                order_count_++;
                return true;
            }
        }
        return false;
    }

    bool modify_order(uint64_t order_id, int64_t new_qty, int64_t new_price, uint64_t new_ts) noexcept {
        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (orders_[i].active && orders_[i].order_id == order_id) {
                if (new_qty <= 0) {
                    orders_[i].active = false;
                    order_count_--;
                    return true;
                }

                // If price changed or quantity increased, lose time priority
                if (new_price != orders_[i].price || new_qty > orders_[i].qty) {
                    orders_[i].priority_ts = new_ts;
                }
                orders_[i].price = new_price;
                orders_[i].qty = new_qty;
                return true;
            }
        }
        return false;
    }

    bool cancel_order(uint64_t order_id) noexcept {
        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (orders_[i].active && orders_[i].order_id == order_id) {
                orders_[i].active = false;
                order_count_--;
                return true;
            }
        }
        return false;
    }

    bool execute_order(uint64_t order_id, int64_t exec_qty) noexcept {
        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (orders_[i].active && orders_[i].order_id == order_id) {
                if (exec_qty >= orders_[i].qty) {
                    orders_[i].active = false;
                    order_count_--;
                } else {
                    orders_[i].qty -= exec_qty;
                }
                return true;
            }
        }
        return false;
    }

    // Computes the total quantity resting ahead of a specific order in queue at its price level
    int64_t compute_queue_ahead(uint64_t order_id) const noexcept {
        const MboOrder* target = nullptr;
        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (orders_[i].active && orders_[i].order_id == order_id) {
                target = &orders_[i];
                break;
            }
        }
        if (!target) return -1; // Order not found

        int64_t ahead_qty = 0;
        for (size_t i = 0; i < kMaxOrders; ++i) {
            if (orders_[i].active &&
                orders_[i].symbol_idx == target->symbol_idx &&
                orders_[i].side == target->side &&
                orders_[i].price == target->price &&
                orders_[i].order_id != target->order_id)
            {
                if (orders_[i].priority_ts < target->priority_ts) {
                    ahead_qty += orders_[i].qty;
                }
            }
        }
        return ahead_qty;
    }

    size_t order_count() const noexcept { return order_count_; }

private:
    std::array<MboOrder, kMaxOrders> orders_{};
    size_t order_count_{0};
};

} // namespace mbo
} // namespace luv
