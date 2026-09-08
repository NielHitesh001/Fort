#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

enum class ContingencyType : uint8_t {
    OCO = 0, // One-Cancels-Other (e.g. Take-Profit limit + Stop-Loss stop)
    OTO = 1, // One-Triggers-Other (Primary fill triggers bracket children)
    TrailingStop = 2 // Stop price moves dynamically with favorable price peaks
};

enum class ContingentState : uint8_t {
    Inactive = 0,
    Active = 1,
    Triggered = 2,
    Cancelled = 3,
    Filled = 4
};

struct ContingentOrder {
    uint64_t order_id{0};
    uint64_t parent_id{0};
    uint64_t pair_order_id{0}; // For OCO
    bool is_buy{true};
    uint64_t limit_price{0};
    uint64_t stop_price{0};
    uint64_t trailing_delta{0}; // Trailing offset from peak
    uint64_t peak_price{0};     // Peak price seen for trailing stop
    uint64_t quantity{0};
    ContingencyType type{ContingencyType::OCO};
    ContingentState state{ContingentState::Inactive};
};

class ContingentOrderManager {
public:
    static constexpr size_t kMaxOrders = 128;

    ContingentOrderManager() noexcept : order_count_(0) {}

    // Register OCO Pair
    bool add_oco_pair(
        uint64_t order_a_id, uint64_t limit_price,
        uint64_t order_b_id, uint64_t stop_price,
        bool is_buy, uint64_t qty) noexcept
    {
        if (order_count_ + 2 > kMaxOrders) return false;

        auto& a = orders_[order_count_++];
        a.order_id = order_a_id;
        a.pair_order_id = order_b_id;
        a.is_buy = is_buy;
        a.limit_price = limit_price;
        a.quantity = qty;
        a.type = ContingencyType::OCO;
        a.state = ContingentState::Active;

        auto& b = orders_[order_count_++];
        b.order_id = order_b_id;
        b.pair_order_id = order_a_id;
        b.is_buy = is_buy;
        b.stop_price = stop_price;
        b.quantity = qty;
        b.type = ContingencyType::OCO;
        b.state = ContingentState::Active;

        return true;
    }

    // Register Trailing Stop
    bool add_trailing_stop(uint64_t order_id, bool is_buy, uint64_t initial_price, uint64_t trailing_delta, uint64_t qty) noexcept {
        if (order_count_ >= kMaxOrders) return false;

        auto& ord = orders_[order_count_++];
        ord.order_id = order_id;
        ord.is_buy = is_buy;
        ord.trailing_delta = trailing_delta;
        ord.peak_price = initial_price;
        ord.quantity = qty;
        ord.type = ContingencyType::TrailingStop;
        ord.state = ContingentState::Active;

        if (is_buy) {
            // For Buy trailing stop: triggers if price bounces trailing_delta above low peak
            ord.stop_price = initial_price + trailing_delta;
        } else {
            // For Sell trailing stop: triggers if price drops trailing_delta below high peak
            ord.stop_price = (initial_price > trailing_delta) ? (initial_price - trailing_delta) : 0;
        }

        return true;
    }

    // Process market price tick for trailing stops and stop triggers
    void on_market_price(uint64_t current_price) noexcept {
        for (size_t i = 0; i < order_count_; ++i) {
            auto& ord = orders_[i];
            if (ord.state != ContingentState::Active) continue;

            if (ord.type == ContingencyType::TrailingStop) {
                if (!ord.is_buy) {
                    // Sell stop: update peak if price climbs higher
                    if (current_price > ord.peak_price) {
                        ord.peak_price = current_price;
                        ord.stop_price = (current_price > ord.trailing_delta) ? (current_price - ord.trailing_delta) : 0;
                    } else if (current_price <= ord.stop_price && ord.stop_price > 0) {
                        ord.state = ContingentState::Triggered;
                    }
                } else {
                    // Buy stop: update trough if price drops lower
                    if (current_price < ord.peak_price) {
                        ord.peak_price = current_price;
                        ord.stop_price = current_price + ord.trailing_delta;
                    } else if (current_price >= ord.stop_price) {
                        ord.state = ContingentState::Triggered;
                    }
                }
            }
        }
    }

    // Process order fill (cancels paired order in OCO)
    void on_order_fill(uint64_t order_id) noexcept {
        for (size_t i = 0; i < order_count_; ++i) {
            if (orders_[i].order_id == order_id) {
                orders_[i].state = ContingentState::Filled;
                uint64_t pair_id = orders_[i].pair_order_id;
                if (orders_[i].type == ContingencyType::OCO && pair_id > 0) {
                    cancel_order(pair_id);
                }
                break;
            }
        }
    }

    void cancel_order(uint64_t order_id) noexcept {
        for (size_t i = 0; i < order_count_; ++i) {
            if (orders_[i].order_id == order_id && orders_[i].state == ContingentState::Active) {
                orders_[i].state = ContingentState::Cancelled;
                break;
            }
        }
    }

    const ContingentOrder* get_order(uint64_t order_id) const noexcept {
        for (size_t i = 0; i < order_count_; ++i) {
            if (orders_[i].order_id == order_id) return &orders_[i];
        }
        return nullptr;
    }

private:
    std::array<ContingentOrder, kMaxOrders> orders_{};
    size_t order_count_{0};
};

} // namespace luv
