#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include "luv_execution.hpp"

namespace luv {

enum class StpMode : uint8_t {
    kNone = 0,
    kCancelNewest = 1,      // CN: Reject/cancel incoming aggressor order
    kCancelOldest = 2,      // CO: Cancel resting passive order
    kCancelBoth = 3,        // CB: Cancel both passive and aggressive orders
    kDecrementAndCancel = 4 // DC: Decrement larger order by smaller order size, cancel remainder of smaller
};

struct StpResolution {
    bool self_trade_detected = false;
    bool cancel_incoming = false;
    bool cancel_resting = false;
    int64_t adjusted_incoming_qty = 0;
    int64_t adjusted_resting_qty = 0;
};

class SelfTradePreventionEngine {
public:
    static StpResolution evaluate(
        uint32_t incoming_firm_id,
        uint32_t resting_firm_id,
        StpMode mode,
        int64_t incoming_qty,
        int64_t resting_qty) noexcept
    {
        StpResolution res;
        if (incoming_firm_id == 0 || resting_firm_id == 0 || incoming_firm_id != resting_firm_id || mode == StpMode::kNone) {
            res.self_trade_detected = false;
            res.adjusted_incoming_qty = incoming_qty;
            res.adjusted_resting_qty = resting_qty;
            return res;
        }

        res.self_trade_detected = true;
        switch (mode) {
            case StpMode::kCancelNewest:
                res.cancel_incoming = true;
                res.cancel_resting = false;
                res.adjusted_incoming_qty = 0;
                res.adjusted_resting_qty = resting_qty;
                break;

            case StpMode::kCancelOldest:
                res.cancel_incoming = false;
                res.cancel_resting = true;
                res.adjusted_incoming_qty = incoming_qty;
                res.adjusted_resting_qty = 0;
                break;

            case StpMode::kCancelBoth:
                res.cancel_incoming = true;
                res.cancel_resting = true;
                res.adjusted_incoming_qty = 0;
                res.adjusted_resting_qty = 0;
                break;

            case StpMode::kDecrementAndCancel:
                if (incoming_qty == resting_qty) {
                    res.cancel_incoming = true;
                    res.cancel_resting = true;
                    res.adjusted_incoming_qty = 0;
                    res.adjusted_resting_qty = 0;
                } else if (incoming_qty > resting_qty) {
                    res.cancel_incoming = false;
                    res.cancel_resting = true;
                    res.adjusted_incoming_qty = incoming_qty - resting_qty;
                    res.adjusted_resting_qty = 0;
                } else {
                    res.cancel_incoming = true;
                    res.cancel_resting = false;
                    res.adjusted_incoming_qty = 0;
                    res.adjusted_resting_qty = resting_qty - incoming_qty;
                }
                break;

            case StpMode::kNone:
            default:
                break;
        }

        return res;
    }
};

// Market Maker Protection (MMP) Window Tracker
struct MmpConfig {
    uint64_t window_ns = 500'000'000; // 500ms sliding window
    int64_t max_traded_volume = 10'000;
    int64_t max_traded_notional = 100'000'000; // $1,000,000 in cents
    int64_t max_delta_volume = 5'000;
};

class MarketMakerProtection {
public:
    explicit MarketMakerProtection(const MmpConfig& config) noexcept
        : config_(config) {}

    // Record an execution for the market maker
    bool on_trade(uint64_t ts_ns, uint8_t side, int64_t qty, int64_t price) noexcept {
        if (tripped_) return false;

        // Slide window
        if (ts_ns > window_start_ts_ns_ + config_.window_ns) {
            window_start_ts_ns_ = ts_ns;
            volume_in_window_ = 0;
            notional_in_window_ = 0;
            delta_in_window_ = 0;
        }

        volume_in_window_ += qty;
        notional_in_window_ += (qty * price);
        if (side == exec::kBuy) {
            delta_in_window_ += qty;
        } else {
            delta_in_window_ -= qty;
        }

        const int64_t abs_delta = (delta_in_window_ < 0) ? -delta_in_window_ : delta_in_window_;

        // Check if limits breached
        if (volume_in_window_ > config_.max_traded_volume ||
            notional_in_window_ > config_.max_traded_notional ||
            abs_delta > config_.max_delta_volume)
        {
            tripped_ = true;
            trip_ts_ns_ = ts_ns;
            return false;
        }

        return true;
    }

    bool is_tripped() const noexcept { return tripped_; }
    uint64_t trip_ts_ns() const noexcept { return trip_ts_ns_; }

    void reset() noexcept {
        tripped_ = false;
        trip_ts_ns_ = 0;
        volume_in_window_ = 0;
        notional_in_window_ = 0;
        delta_in_window_ = 0;
        window_start_ts_ns_ = 0;
    }

private:
    MmpConfig config_;
    bool tripped_{false};
    uint64_t trip_ts_ns_{0};
    uint64_t window_start_ts_ns_{0};
    int64_t volume_in_window_{0};
    int64_t notional_in_window_{0};
    int64_t delta_in_window_{0};
};

} // namespace luv
