#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace luv {

enum class DisruptivePattern : uint8_t {
    None = 0,
    Spoofing = 1,        // Large quote entered with no intent to execute, cancelled rapidly
    Flipping = 2,        // Quote on side A, filled on small side B, side A instantly cancelled
    QuoteStuffing = 3    // Massive burst of orders/cancels overwhelming venue within <1ms
};

struct TradeAuditEntry {
    uint64_t timestamp_ns{0};
    uint64_t order_id{0};
    bool is_buy{true};
    uint64_t price{0};
    uint64_t quantity{0};
    bool is_cancel{false};
    bool is_fill{false};
};

struct SurveillanceAlert {
    uint64_t timestamp_ns{0};
    uint64_t trader_id{0};
    DisruptivePattern pattern{DisruptivePattern::None};
    uint64_t severity_score{0}; // 0-100
};

class CftcRule575Validator {
public:
    static constexpr size_t kHistoryCapacity = 128;
    static constexpr uint64_t kRapidCancelNs = 5'000'000ULL; // 5 milliseconds

    CftcRule575Validator() noexcept : entry_count_(0) {}

    void on_order_entry(uint64_t now_ns, uint64_t order_id, bool is_buy, uint64_t price, uint64_t qty) noexcept {
        add_entry(now_ns, order_id, is_buy, price, qty, false, false);
    }

    void on_order_cancel(uint64_t now_ns, uint64_t order_id) noexcept {
        add_entry(now_ns, order_id, false, 0, 0, true, false);
    }

    void on_order_fill(uint64_t now_ns, uint64_t order_id, uint64_t fill_qty) noexcept {
        add_entry(now_ns, order_id, false, 0, fill_qty, false, true);
    }

    // Evaluates recent activity window for disruptive patterns (CFTC 7 U.S.C. 6c(a)(5) / CME Rule 575)
    SurveillanceAlert evaluate_activity(uint64_t trader_id, uint64_t now_ns) const noexcept {
        SurveillanceAlert alert;
        alert.timestamp_ns = now_ns;
        alert.trader_id = trader_id;
        alert.pattern = DisruptivePattern::None;

        if (entry_count_ < 4) return alert;

        size_t total_orders = 0;
        size_t total_cancels = 0;
        size_t rapid_cancels = 0;

        for (size_t i = 0; i < entry_count_; ++i) {
            if (!entries_[i].is_cancel && !entries_[i].is_fill) {
                ++total_orders;
                uint64_t oid = entries_[i].order_id;
                uint64_t entry_time = entries_[i].timestamp_ns;

                // Look for matching cancel
                for (size_t j = i + 1; j < entry_count_; ++j) {
                    if (entries_[j].order_id == oid && entries_[j].is_cancel) {
                        ++total_cancels;
                        if (entries_[j].timestamp_ns <= entry_time + kRapidCancelNs) {
                            ++rapid_cancels;
                        }
                        break;
                    }
                }
            }
        }

        // 1. Quote Stuffing check (>20 messages within 1 millisecond)
        if (entry_count_ >= 20) {
            uint64_t first_t = entries_[entry_count_ - 20].timestamp_ns;
            uint64_t last_t = entries_[entry_count_ - 1].timestamp_ns;
            if (last_t <= first_t + 1'000'000ULL) { // 1ms
                alert.pattern = DisruptivePattern::QuoteStuffing;
                alert.severity_score = 95;
                return alert;
            }
        }

        // 2. Spoofing check: high volume entry followed by >80% rapid cancellation
        if (total_orders >= 5 && rapid_cancels * 100 / total_orders >= 80) {
            alert.pattern = DisruptivePattern::Spoofing;
            alert.severity_score = 90;
            return alert;
        }

        return alert;
    }

private:
    void add_entry(uint64_t now_ns, uint64_t order_id, bool is_buy, uint64_t price, uint64_t qty, bool is_cancel, bool is_fill) noexcept {
        if (entry_count_ >= kHistoryCapacity) {
            // Shift left by 1
            for (size_t i = 1; i < kHistoryCapacity; ++i) {
                entries_[i - 1] = entries_[i];
            }
            --entry_count_;
        }
        entries_[entry_count_++] = {now_ns, order_id, is_buy, price, qty, is_cancel, is_fill};
    }

    std::array<TradeAuditEntry, kHistoryCapacity> entries_{};
    size_t entry_count_{0};
};

} // namespace luv
