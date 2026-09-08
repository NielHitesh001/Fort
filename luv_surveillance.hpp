#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace surveillance {

enum class SurveillanceAlert : uint8_t {
    kNone = 0,
    kSpoofingLayering = 1,
    kQuoteStuffing = 2,
    kWashTrading = 3
};

struct SurveillanceEvent {
    uint32_t firm_id = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    uint8_t event_type = 0; // 1=New, 2=Cancel, 3=Fill
    int64_t qty = 0;
    uint64_t ts_ns = 0;
};

struct SurveillanceConfig {
    uint64_t burst_window_ns = 10'000'000; // 10ms burst window
    uint32_t max_burst_messages = 500;     // > 500 msgs in 10ms -> Quote stuffing
    double spoofing_cancel_ratio = 0.80;   // > 80% cancellation rate with opposite fill -> Spoofing
    uint32_t min_spoofing_samples = 10;
};

class MarketSurveillanceEngine {
public:
    static constexpr size_t kHistorySize = 1024;

    explicit MarketSurveillanceEngine(const SurveillanceConfig& config = SurveillanceConfig{}) noexcept
        : config_(config), count_(0) {}

    // Ingests an event and checks for abusive market manipulation patterns
    SurveillanceAlert observe_event(
        uint32_t firm_id,
        uint16_t symbol_idx,
        uint8_t side,
        uint8_t event_type, // 1=New, 2=Cancel, 3=Fill
        int64_t qty,
        uint64_t ts_ns) noexcept
    {
        events_[count_ & (kHistorySize - 1)] = SurveillanceEvent{
            firm_id, symbol_idx, side, event_type, qty, ts_ns
        };
        count_++;

        // 1. Check for Quote Stuffing (Message Burst in 10ms window)
        uint32_t burst_count = 0;
        for (size_t i = 0; i < std::min<size_t>(count_, kHistorySize); ++i) {
            size_t idx = (count_ - 1 - i) & (kHistorySize - 1);
            if (events_[idx].firm_id == firm_id) {
                if (ts_ns <= events_[idx].ts_ns + config_.burst_window_ns) {
                    burst_count++;
                } else {
                    break;
                }
            }
        }
        if (burst_count > config_.max_burst_messages) {
            return SurveillanceAlert::kQuoteStuffing;
        }

        // 2. Check for Spoofing / Layering (Heavy cancellations on side A, execution on side B)
        if (event_type == 3) { // Execution on 'side'
            uint8_t opposite_side = (side == exec::kBuy) ? exec::kSell : exec::kBuy;
            uint32_t opp_new_count = 0;
            uint32_t opp_cancel_count = 0;

            for (size_t i = 0; i < std::min<size_t>(count_, kHistorySize); ++i) {
                size_t idx = (count_ - 1 - i) & (kHistorySize - 1);
                if (events_[idx].firm_id == firm_id && events_[idx].symbol_idx == symbol_idx) {
                    if (events_[idx].side == opposite_side) {
                        if (events_[idx].event_type == 1) opp_new_count++;
                        else if (events_[idx].event_type == 2) opp_cancel_count++;
                    }
                }
            }

            if (opp_new_count >= config_.min_spoofing_samples && opp_cancel_count > 0) {
                double cancel_ratio = static_cast<double>(opp_cancel_count) / static_cast<double>(opp_new_count);
                if (cancel_ratio >= config_.spoofing_cancel_ratio) {
                    return SurveillanceAlert::kSpoofingLayering;
                }
            }
        }

        return SurveillanceAlert::kNone;
    }

private:
    SurveillanceConfig config_;
    std::array<SurveillanceEvent, kHistorySize> events_{};
    size_t count_{0};
};

} // namespace surveillance
} // namespace luv
