#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

enum class MarInfractionType : uint8_t {
    None = 0,
    LayeringAndSpoofing = 1,  // MAR Art. 12(1)(a)(ii) & (b): Non-genuine multi-level orders cancelled after fill
    WashTrading = 2,          // MAR Art. 12(1)(a)(i): Matching buy and sell orders with same beneficial owner
    QuoteStuffing = 3,        // MAR Art. 12(2)(c): High-frequency order/cancel flood impairing venue
    MarkingTheClose = 4,      // MAR Art. 12(2)(a): Abusive aggressive volume injection near fixing/close
    MomentumIgnition = 5      // MAR Art. 12(2)(d): Pre-arranged price spike to trigger stops
};

struct MarAuditRecord {
    uint64_t timestamp_ns{0};
    uint64_t participant_id{0};
    uint64_t order_id{0};
    bool is_buy{true};
    uint64_t price{0};
    uint64_t quantity{0};
    uint32_t depth_level{0};
    bool is_cancel{false};
    bool is_fill{false};
};

struct EsmaMarAlert {
    uint64_t timestamp_ns{0};
    uint64_t participant_id{0};
    MarInfractionType infraction_type{MarInfractionType::None};
    uint32_t confidence_score{0};      // 0 - 100%
    double order_to_trade_ratio{0.0};  // OTR
    double burst_message_rate_hz{0.0}; // Msg / sec
    bool is_alert_triggered{false};
};

class EsmaMarSurveillanceEngine {
public:
    static constexpr size_t kCapacity = 256;
    static constexpr uint64_t kSubMillisecondWindowNs = 5'000'000ULL; // 5 ms
    static constexpr uint64_t kWashTradeWindowNs = 50'000'000ULL;      // 50 ms
    static constexpr uint32_t kQuoteStuffingThreshold = 20;            // 20 msgs in 5 ms (> 4,000 Hz)

    EsmaMarSurveillanceEngine() noexcept : head_(0), count_(0) {}

    void on_order_new(uint64_t timestamp_ns, uint64_t participant_id, uint64_t order_id, 
                      bool is_buy, uint64_t price, uint64_t qty, uint32_t depth_level = 0) noexcept 
    {
        push_record(timestamp_ns, participant_id, order_id, is_buy, price, qty, depth_level, false, false);
    }

    void on_order_cancel(uint64_t timestamp_ns, uint64_t participant_id, uint64_t order_id) noexcept {
        push_record(timestamp_ns, participant_id, order_id, false, 0, 0, 0, true, false);
    }

    void on_order_fill(uint64_t timestamp_ns, uint64_t participant_id, uint64_t order_id, 
                       bool is_buy, uint64_t price, uint64_t fill_qty) noexcept 
    {
        push_record(timestamp_ns, participant_id, order_id, is_buy, price, fill_qty, 0, false, true);
    }

    EsmaMarAlert evaluate_surveillance(uint64_t participant_id, uint64_t now_ns, bool is_market_close_window = false) const noexcept {
        EsmaMarAlert alert{};
        alert.timestamp_ns = now_ns;
        alert.participant_id = participant_id;

        if (count_ < 2) return alert;

        size_t order_count = 0;
        size_t cancel_count = 0;
        size_t fill_count = 0;
        size_t burst_count = 0;
        size_t multi_level_layered = 0;
        size_t rapid_cancels_after_fill = 0;

        uint64_t latest_fill_ts = 0;

        // Wash trade detection state
        uint64_t recent_buy_price = 0;
        uint64_t recent_buy_qty = 0;
        uint64_t recent_buy_ts = 0;
        uint64_t recent_sell_price = 0;
        uint64_t recent_sell_qty = 0;
        uint64_t recent_sell_ts = 0;
        bool wash_trade_detected = false;

        for (size_t i = 0; i < count_; ++i) {
            const auto& rec = records_[i];
            if (rec.participant_id != participant_id) continue;

            // Quote stuffing burst check (within 5ms)
            if (now_ns >= rec.timestamp_ns && (now_ns - rec.timestamp_ns) <= kSubMillisecondWindowNs) {
                ++burst_count;
            }

            if (rec.is_fill) {
                ++fill_count;
                latest_fill_ts = rec.timestamp_ns;
            } else if (rec.is_cancel) {
                ++cancel_count;
                if (latest_fill_ts > 0 && rec.timestamp_ns >= latest_fill_ts && 
                    (rec.timestamp_ns - latest_fill_ts) <= kSubMillisecondWindowNs) {
                    ++rapid_cancels_after_fill;
                }
            } else {
                // New order
                ++order_count;
                if (rec.depth_level >= 2) {
                    ++multi_level_layered;
                }

                if (rec.is_buy) {
                    recent_buy_price = rec.price;
                    recent_buy_qty = rec.quantity;
                    recent_buy_ts = rec.timestamp_ns;
                } else {
                    recent_sell_price = rec.price;
                    recent_sell_qty = rec.quantity;
                    recent_sell_ts = rec.timestamp_ns;
                }

                // Check for wash trade: buy and sell at exact same price/qty within wash window
                if (recent_buy_price > 0 && recent_sell_price > 0 &&
                    recent_buy_price == recent_sell_price &&
                    recent_buy_qty == recent_sell_qty) 
                {
                    uint64_t delta_t = (recent_buy_ts > recent_sell_ts) ? 
                                       (recent_buy_ts - recent_sell_ts) : 
                                       (recent_sell_ts - recent_buy_ts);
                    if (delta_t <= kWashTradeWindowNs) {
                        wash_trade_detected = true;
                    }
                }
            }
        }

        // Calculate Order-to-Trade Ratio (OTR)
        alert.order_to_trade_ratio = (fill_count > 0) ? 
            (static_cast<double>(order_count + cancel_count) / static_cast<double>(fill_count)) : 
            static_cast<double>(order_count + cancel_count);

        alert.burst_message_rate_hz = static_cast<double>(burst_count) / (static_cast<double>(kSubMillisecondWindowNs) / 1e9);

        // Pattern 1: Quote Stuffing
        if (burst_count >= kQuoteStuffingThreshold) {
            alert.infraction_type = MarInfractionType::QuoteStuffing;
            alert.confidence_score = std::min(100U, 60U + static_cast<uint32_t>(burst_count * 2));
            alert.is_alert_triggered = true;
            return alert;
        }

        // Pattern 2: Wash Trading
        if (wash_trade_detected) {
            alert.infraction_type = MarInfractionType::WashTrading;
            alert.confidence_score = 95U;
            alert.is_alert_triggered = true;
            return alert;
        }

        // Pattern 3: Layering & Spoofing
        if (multi_level_layered >= 3 && rapid_cancels_after_fill >= 2 && alert.order_to_trade_ratio >= 4.0) {
            alert.infraction_type = MarInfractionType::LayeringAndSpoofing;
            alert.confidence_score = 90U;
            alert.is_alert_triggered = true;
            return alert;
        }

        // Pattern 4: Marking the Close
        if (is_market_close_window && order_count >= 5 && fill_count >= 3 && alert.order_to_trade_ratio <= 2.0) {
            alert.infraction_type = MarInfractionType::MarkingTheClose;
            alert.confidence_score = 85U;
            alert.is_alert_triggered = true;
            return alert;
        }

        return alert;
    }

private:
    std::array<MarAuditRecord, kCapacity> records_{};
    size_t head_{0};
    size_t count_{0};

    void push_record(uint64_t ts, uint64_t pid, uint64_t oid, bool is_buy, uint64_t price, 
                     uint64_t qty, uint32_t depth, bool is_cancel, bool is_fill) noexcept 
    {
        records_[head_] = MarAuditRecord{ts, pid, oid, is_buy, price, qty, depth, is_cancel, is_fill};
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) {
            ++count_;
        }
    }
};

} // namespace luv
