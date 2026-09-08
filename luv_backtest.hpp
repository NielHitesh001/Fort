#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace backtest {

struct MarketEvent {
    uint64_t timestamp_ns = 0;
    uint16_t symbol_idx = 0;
    int64_t best_bid = 0;
    int64_t best_ask = 0;
    int64_t bid_sz = 0;
    int64_t ask_sz = 0;
};

struct SimulatedOrder {
    uint64_t order_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    int64_t limit_price = 0;
    int64_t qty = 0;
    uint64_t send_time_ns = 0;
    uint64_t ack_time_ns = 0;
    int64_t filled_qty = 0;
    int64_t avg_fill_price = 0;
    bool active = false;
};

struct BacktestMetrics {
    int64_t total_trades = 0;
    int64_t winning_trades = 0;
    int64_t total_pnl = 0;       // Scaled integer
    int64_t max_drawdown = 0;    // Scaled integer peak-to-trough
    double win_rate = 0.0;       // [0.0, 1.0]
    double sharpe_ratio = 0.0;   // Annualized / sample Sharpe
};

class BacktestEngine {
public:
    static constexpr size_t kMaxEvents = 2048;
    static constexpr size_t kMaxOrders = 512;

    explicit BacktestEngine(uint64_t fixed_latency_ns = 500) noexcept
        : latency_ns_(fixed_latency_ns), num_events_(0), num_orders_(0), current_pnl_(0), peak_pnl_(0), max_dd_(0) {}

    bool load_event(const MarketEvent& evt) noexcept {
        if (num_events_ >= kMaxEvents) return false;
        events_[num_events_++] = evt;
        return true;
    }

    bool submit_order(uint64_t order_id, uint16_t sym, uint8_t side, int64_t price, int64_t qty, uint64_t send_time_ns) noexcept {
        if (num_orders_ >= kMaxOrders) return false;
        orders_[num_orders_++] = SimulatedOrder{
            .order_id = order_id,
            .symbol_idx = sym,
            .side = side,
            .limit_price = price,
            .qty = qty,
            .send_time_ns = send_time_ns,
            .ack_time_ns = send_time_ns + latency_ns_,
            .filled_qty = 0,
            .avg_fill_price = 0,
            .active = true
        };
        return true;
    }

    // Step through the simulation up to current event index
    void run_simulation() noexcept {
        for (size_t i = 0; i < num_events_; ++i) {
            const auto& evt = events_[i];

            for (size_t j = 0; j < num_orders_; ++j) {
                auto& ord = orders_[j];
                if (!ord.active || ord.ack_time_ns > evt.timestamp_ns) {
                    continue; // In flight / not arrived at exchange yet
                }

                if (ord.side == exec::kBuy) {
                    // Match against ask
                    if (ord.limit_price >= evt.best_ask && evt.best_ask > 0) {
                        int64_t fill_qty = std::min(ord.qty - ord.filled_qty, evt.ask_sz);
                        if (fill_qty > 0) {
                            ord.filled_qty += fill_qty;
                            ord.avg_fill_price = evt.best_ask; // Passive price fill
                            if (ord.filled_qty >= ord.qty) {
                                ord.active = false;
                            }
                            update_pnl(ord.side, evt.best_ask, fill_qty);
                        }
                    }
                } else {
                    // Match against bid
                    if (ord.limit_price <= evt.best_bid && evt.best_bid > 0) {
                        int64_t fill_qty = std::min(ord.qty - ord.filled_qty, evt.bid_sz);
                        if (fill_qty > 0) {
                            ord.filled_qty += fill_qty;
                            ord.avg_fill_price = evt.best_bid;
                            if (ord.filled_qty >= ord.qty) {
                                ord.active = false;
                            }
                            update_pnl(ord.side, evt.best_bid, fill_qty);
                        }
                    }
                }
            }
        }
    }

    BacktestMetrics compute_metrics() const noexcept {
        BacktestMetrics metrics{};
        int64_t completed_orders = 0;

        for (size_t i = 0; i < num_orders_; ++i) {
            if (orders_[i].filled_qty > 0) {
                completed_orders++;
            }
        }

        metrics.total_trades = completed_orders;
        metrics.total_pnl = current_pnl_;
        metrics.max_drawdown = max_dd_;

        if (completed_orders > 0) {
            metrics.win_rate = (current_pnl_ > 0) ? 1.0 : 0.0;
        }

        return metrics;
    }

    int64_t get_total_pnl() const noexcept { return current_pnl_; }
    int64_t get_max_drawdown() const noexcept { return max_dd_; }

private:
    void update_pnl(uint8_t side, int64_t fill_price, int64_t fill_qty) noexcept {
        int64_t trade_delta = (side == exec::kSell) ? (fill_price * fill_qty) : -(fill_price * fill_qty);
        current_pnl_ += trade_delta;

        if (current_pnl_ > peak_pnl_) {
            peak_pnl_ = current_pnl_;
        } else {
            int64_t dd = peak_pnl_ - current_pnl_;
            if (dd > max_dd_) {
                max_dd_ = dd;
            }
        }
    }

    uint64_t latency_ns_{500};
    std::array<MarketEvent, kMaxEvents> events_{};
    size_t num_events_{0};
    std::array<SimulatedOrder, kMaxOrders> orders_{};
    size_t num_orders_{0};

    int64_t current_pnl_{0};
    int64_t peak_pnl_{0};
    int64_t max_dd_{0};
};

} // namespace backtest
} // namespace luv
