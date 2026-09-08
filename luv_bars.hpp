#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace bars {

struct OhlcvBar {
    uint64_t start_time_ns = 0;
    uint64_t end_time_ns = 0;
    uint16_t symbol_idx = 0;
    int64_t open_price = 0;
    int64_t high_price = 0;
    int64_t low_price = 0;
    int64_t close_price = 0;
    int64_t volume = 0;
    int64_t vwap_numerator = 0; // sum(price * qty)
    uint32_t trade_count = 0;
    bool completed = false;

    int64_t vwap() const noexcept {
        return (volume > 0) ? (vwap_numerator / volume) : close_price;
    }
};

template <size_t IntervalSec = 60>
class TimeBarAggregator {
public:
    static constexpr uint64_t kIntervalNs = IntervalSec * 1'000'000'000ULL;

    TimeBarAggregator() noexcept : active_(false) {
        current_bar_ = OhlcvBar{};
    }

    // Ingests executed trade tick. Returns true if a bar was completed on this tick.
    bool on_trade(
        uint16_t symbol_idx,
        int64_t price,
        int64_t qty,
        uint64_t ts_ns,
        OhlcvBar& out_completed_bar) noexcept
    {
        if (price <= 0 || qty <= 0) return false;

        const uint64_t bucket_start = (ts_ns / kIntervalNs) * kIntervalNs;
        bool bar_completed = false;

        if (!active_) {
            // First bar initialization
            init_bar(symbol_idx, price, qty, bucket_start);
            active_ = true;
        } else if (bucket_start > current_bar_.start_time_ns) {
            // New interval -> finalize previous bar
            current_bar_.end_time_ns = current_bar_.start_time_ns + kIntervalNs;
            current_bar_.completed = true;
            out_completed_bar = current_bar_;
            bar_completed = true;

            // Start new bar
            init_bar(symbol_idx, price, qty, bucket_start);
        } else {
            // Update current bar
            current_bar_.high_price = std::max(current_bar_.high_price, price);
            current_bar_.low_price = std::min(current_bar_.low_price, price);
            current_bar_.close_price = price;
            current_bar_.volume += qty;
            current_bar_.vwap_numerator += (price * qty);
            current_bar_.trade_count++;
        }

        return bar_completed;
    }

    const OhlcvBar& current_bar() const noexcept { return current_bar_; }

private:
    void init_bar(uint16_t symbol_idx, int64_t price, int64_t qty, uint64_t bucket_start) noexcept {
        current_bar_.start_time_ns = bucket_start;
        current_bar_.end_time_ns = bucket_start + kIntervalNs;
        current_bar_.symbol_idx = symbol_idx;
        current_bar_.open_price = price;
        current_bar_.high_price = price;
        current_bar_.low_price = price;
        current_bar_.close_price = price;
        current_bar_.volume = qty;
        current_bar_.vwap_numerator = (price * qty);
        current_bar_.trade_count = 1;
        current_bar_.completed = false;
    }

    bool active_{false};
    OhlcvBar current_bar_{};
};

} // namespace bars
} // namespace luv
