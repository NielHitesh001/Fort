#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace microstructure {

struct VolumeBucket {
    int64_t buy_volume = 0;
    int64_t sell_volume = 0;
    bool is_completed = false;
};

class VpinCalculator {
public:
    static constexpr size_t kBucketCount = 50; // Rolling window of 50 volume buckets (Easley et al.)
    static constexpr int64_t kDefaultBucketSize = 10'000; // 10k shares per bucket

    explicit VpinCalculator(int64_t bucket_size = kDefaultBucketSize) noexcept
        : bucket_size_(bucket_size), current_bucket_idx_(0), completed_buckets_(0) {}

    // Ingests trade fill and fills rolling volume buckets
    void add_trade(uint8_t side, int64_t qty) noexcept {
        if (qty <= 0) return;

        int64_t remaining_qty = qty;

        while (remaining_qty > 0) {
            auto& curr = buckets_[current_bucket_idx_];
            int64_t curr_total = curr.buy_volume + curr.sell_volume;
            int64_t needed = bucket_size_ - curr_total;

            int64_t fill = std::min(remaining_qty, needed);
            if (side == exec::kBuy) {
                curr.buy_volume += fill;
            } else {
                curr.sell_volume += fill;
            }

            remaining_qty -= fill;

            if (curr.buy_volume + curr.sell_volume >= bucket_size_) {
                curr.is_completed = true;
                completed_buckets_++;
                current_bucket_idx_ = (current_bucket_idx_ + 1) % kBucketCount;
                // Reset new bucket
                buckets_[current_bucket_idx_] = VolumeBucket{};
            }
        }
    }

    // Computes VPIN = sum(|V_buy - V_sell|) / (N * V_bucket)
    double compute_vpin() const noexcept {
        size_t n = std::min(completed_buckets_, kBucketCount);
        if (n == 0) return 0.0;

        int64_t total_order_imbalance = 0;

        for (size_t i = 0; i < kBucketCount; ++i) {
            if (buckets_[i].is_completed) {
                total_order_imbalance += std::abs(buckets_[i].buy_volume - buckets_[i].sell_volume);
            }
        }

        double total_volume = static_cast<double>(n * bucket_size_);
        return static_cast<double>(total_order_imbalance) / total_volume;
    }

private:
    int64_t bucket_size_{kDefaultBucketSize};
    std::array<VolumeBucket, kBucketCount> buckets_{};
    size_t current_bucket_idx_{0};
    size_t completed_buckets_{0};
};

} // namespace microstructure
} // namespace luv
