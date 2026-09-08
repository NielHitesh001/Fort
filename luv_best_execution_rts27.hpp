#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace best_ex {

struct ExecutionQualityRecord {
    uint64_t order_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    int64_t arrival_midpoint = 0; // Scaled x10,000
    int64_t fill_price = 0;       // Scaled x10,000
    int64_t fill_qty = 0;
    uint64_t latency_ns = 0;      // Order receipt to execution latency in ns
};

struct BestExRts27Metrics {
    int64_t total_executed_qty = 0;
    int64_t total_orders = 0;
    int64_t total_price_improved_orders = 0;
    int64_t total_price_improved_qty = 0;
    double avg_effective_spread_bps = 0.0;
    uint64_t p50_latency_ns = 0;
    uint64_t p95_latency_ns = 0;
    uint64_t p99_latency_ns = 0;
};

class BestExecutionRts27Engine {
public:
    static constexpr size_t kMaxRecords = 512;

    BestExecutionRts27Engine() noexcept : num_records_(0) {}

    bool record_execution(const ExecutionQualityRecord& rec) noexcept {
        if (num_records_ >= kMaxRecords) return false;
        records_[num_records_++] = rec;
        return true;
    }

    BestExRts27Metrics generate_rts27_report() const noexcept {
        BestExRts27Metrics metrics{};
        if (num_records_ == 0) return metrics;

        metrics.total_orders = static_cast<int64_t>(num_records_);

        double sum_effective_spread = 0.0;
        std::array<uint64_t, kMaxRecords> latencies{};

        for (size_t i = 0; i < num_records_; ++i) {
            const auto& rec = records_[i];
            metrics.total_executed_qty += rec.fill_qty;
            latencies[i] = rec.latency_ns;

            // Effective spread = 2 * |fill_price - arrival_midpoint| / arrival_midpoint (in bps)
            if (rec.arrival_midpoint > 0) {
                int64_t diff = std::abs(rec.fill_price - rec.arrival_midpoint);
                double eff_spread_bps = (2.0 * static_cast<double>(diff) / static_cast<double>(rec.arrival_midpoint)) * 10000.0;
                sum_effective_spread += eff_spread_bps;

                // Price improvement check:
                // Buy fill below midpoint OR Sell fill above midpoint
                if ((rec.side == exec::kBuy && rec.fill_price < rec.arrival_midpoint) ||
                    (rec.side == exec::kSell && rec.fill_price > rec.arrival_midpoint)) {
                    metrics.total_price_improved_orders++;
                    metrics.total_price_improved_qty += rec.fill_qty;
                }
            }
        }

        metrics.avg_effective_spread_bps = sum_effective_spread / static_cast<double>(num_records_);

        std::sort(latencies.begin(), latencies.begin() + num_records_);
        metrics.p50_latency_ns = latencies[num_records_ / 2];
        metrics.p95_latency_ns = latencies[(num_records_ * 95) / 100];
        size_t p99_idx = (num_records_ * 99) / 100;
        if (p99_idx >= num_records_) p99_idx = num_records_ - 1;
        metrics.p99_latency_ns = latencies[p99_idx];

        return metrics;
    }

private:
    std::array<ExecutionQualityRecord, kMaxRecords> records_{};
    size_t num_records_{0};
};

} // namespace best_ex
} // namespace luv
