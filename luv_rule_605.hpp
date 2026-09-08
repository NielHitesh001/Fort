#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace compliance {

enum class Rule605OrderType : uint8_t {
    kMarket = 0,
    kMarketableLimit = 1,
    kInsideTheQuote = 2,
    kAtTheQuote = 3,
    kNearTheQuote = 4
};

struct Rule605OrderEntry {
    uint64_t order_id = 0;
    Rule605OrderType order_type = Rule605OrderType::kMarket;
    uint8_t side = exec::kBuy;
    int64_t order_qty = 0;
    int64_t executed_qty = 0;
    int64_t quote_midpoint = 0; // Scaled x10,000
    int64_t fill_price = 0;     // Scaled x10,000
    uint64_t exec_latency_ns = 0;
};

struct Rule605MonthlyReport {
    int64_t total_covered_orders = 0;
    int64_t total_shares_executed = 0;
    int64_t total_price_improved_shares = 0;
    int64_t total_at_quote_shares = 0;
    int64_t total_outside_quote_shares = 0;
    double avg_realized_spread_bps = 0.0;
    double avg_effective_spread_bps = 0.0;
    double avg_execution_time_ms = 0.0;
};

class Rule605Reporter {
public:
    static constexpr size_t kMaxOrders = 512;

    Rule605Reporter() noexcept : num_orders_(0) {}

    bool record_order(const Rule605OrderEntry& entry) noexcept {
        if (num_orders_ >= kMaxOrders) return false;
        orders_[num_orders_++] = entry;
        return true;
    }

    Rule605MonthlyReport generate_monthly_report() const noexcept {
        Rule605MonthlyReport report{};
        if (num_orders_ == 0) return report;

        report.total_covered_orders = static_cast<int64_t>(num_orders_);

        double sum_eff_spread = 0.0;
        double sum_latency_ns = 0.0;

        for (size_t i = 0; i < num_orders_; ++i) {
            const auto& ord = orders_[i];
            report.total_shares_executed += ord.executed_qty;
            sum_latency_ns += static_cast<double>(ord.exec_latency_ns);

            if (ord.quote_midpoint > 0 && ord.executed_qty > 0) {
                int64_t diff = std::abs(ord.fill_price - ord.quote_midpoint);
                double eff_bps = (2.0 * static_cast<double>(diff) / static_cast<double>(ord.quote_midpoint)) * 10000.0;
                sum_eff_spread += eff_bps;

                // Price improvement / Quote classification
                if ((ord.side == exec::kBuy && ord.fill_price < ord.quote_midpoint) ||
                    (ord.side == exec::kSell && ord.fill_price > ord.quote_midpoint)) {
                    report.total_price_improved_shares += ord.executed_qty;
                } else if (ord.fill_price == ord.quote_midpoint) {
                    report.total_at_quote_shares += ord.executed_qty;
                } else {
                    report.total_outside_quote_shares += ord.executed_qty;
                }
            }
        }

        report.avg_effective_spread_bps = sum_eff_spread / static_cast<double>(num_orders_);
        report.avg_execution_time_ms = (sum_latency_ns / static_cast<double>(num_orders_)) / 1'000'000.0;

        return report;
    }

private:
    std::array<Rule605OrderEntry, kMaxOrders> orders_{};
    size_t num_orders_{0};
};

} // namespace compliance
} // namespace luv
