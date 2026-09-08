#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <algorithm>

namespace luv {

struct BookLevelSnapshot {
    double price{0.0};
    uint64_t size{0};
};

struct MultiLevelBookSnapshot {
    static constexpr size_t MAX_LEVELS = 5;
    uint64_t timestamp_ns{0};
    size_t num_levels{0};
    std::array<BookLevelSnapshot, MAX_LEVELS> bids{};
    std::array<BookLevelSnapshot, MAX_LEVELS> asks{};
};

struct OFIConfig {
    static constexpr size_t MAX_LEVELS = 5;
    double beta_price_impact{0.00005}; // Price impact coefficient per OFI unit
    double ema_alpha{0.15};            // Rolling decay factor
    double toxicity_threshold{2500.0}; // Threshold for multi-level OFI alarm
    std::array<double, MAX_LEVELS> level_weights{0.50, 0.25, 0.125, 0.075, 0.05};
};

struct OFIResult {
    uint64_t timestamp_ns{0};
    double level1_ofi{0.0};
    double multi_level_ofi{0.0};
    double rolling_ofi{0.0};
    double predicted_price_delta{0.0};
    bool toxic_imbalance_detected{false};
};

class OrderFlowImbalanceEngine {
public:
    static constexpr size_t MAX_LEVELS = 5;

    explicit OrderFlowImbalanceEngine(const OFIConfig& cfg = OFIConfig{}) noexcept
        : config_(cfg) {
        reset();
    }

    void reset() noexcept {
        has_prev_snapshot_ = false;
        prev_snapshot_ = MultiLevelBookSnapshot{};
        rolling_ofi_ = 0.0;
        event_count_ = 0;
    }

    OFIResult update(const MultiLevelBookSnapshot& current) noexcept {
        OFIResult result{};
        result.timestamp_ns = current.timestamp_ns;

        if (!has_prev_snapshot_) {
            prev_snapshot_ = current;
            has_prev_snapshot_ = true;
            return result;
        }

        size_t levels = std::min({current.num_levels, prev_snapshot_.num_levels, MAX_LEVELS});
        if (levels == 0) {
            prev_snapshot_ = current;
            return result;
        }

        double total_multi_ofi = 0.0;

        for (size_t k = 0; k < levels; ++k) {
            const auto& curr_b = current.bids[k];
            const auto& prev_b = prev_snapshot_.bids[k];
            const auto& curr_a = current.asks[k];
            const auto& prev_a = prev_snapshot_.asks[k];

            // Cont-Kukanov-Stoikov (2014) formula for bid flow (Delta W_b):
            double delta_bid = 0.0;
            if (curr_b.price > prev_b.price) {
                delta_bid = static_cast<double>(curr_b.size);
            } else if (curr_b.price == prev_b.price) {
                delta_bid = static_cast<double>(curr_b.size) - static_cast<double>(prev_b.size);
            } else { // curr_b.price < prev_b.price
                delta_bid = -static_cast<double>(prev_b.size);
            }

            // Cont-Kukanov-Stoikov (2014) formula for ask flow (Delta W_a):
            double delta_ask = 0.0;
            if (curr_a.price < prev_a.price) {
                delta_ask = static_cast<double>(curr_a.size);
            } else if (curr_a.price == prev_a.price) {
                delta_ask = static_cast<double>(curr_a.size) - static_cast<double>(prev_a.size);
            } else { // curr_a.price > prev_a.price
                delta_ask = -static_cast<double>(prev_a.size);
            }

            // OFI_k = Delta W_b - Delta W_a
            double ofi_k = delta_bid - delta_ask;

            if (k == 0) {
                result.level1_ofi = ofi_k;
            }

            total_multi_ofi += config_.level_weights[k] * ofi_k;
        }

        result.multi_level_ofi = total_multi_ofi;

        // Rolling EMA update
        rolling_ofi_ = (config_.ema_alpha * total_multi_ofi) + ((1.0 - config_.ema_alpha) * rolling_ofi_);
        result.rolling_ofi = rolling_ofi_;

        // Predicted instantaneous price move: Delta P = beta * OFI
        result.predicted_price_delta = config_.beta_price_impact * total_multi_ofi;

        // Toxicity detection
        result.toxic_imbalance_detected = (std::abs(total_multi_ofi) >= config_.toxicity_threshold) ||
                                          (std::abs(rolling_ofi_) >= (config_.toxicity_threshold * 0.75));

        prev_snapshot_ = current;
        ++event_count_;
        return result;
    }

    uint64_t event_count() const noexcept { return event_count_; }
    double rolling_ofi() const noexcept { return rolling_ofi_; }

private:
    OFIConfig config_{};
    MultiLevelBookSnapshot prev_snapshot_{};
    bool has_prev_snapshot_{false};
    double rolling_ofi_{0.0};
    uint64_t event_count_{0};
};

} // namespace luv
