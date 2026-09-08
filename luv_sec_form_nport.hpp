#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

// SEC Rule 22e-4 Liquidity Classification Buckets
enum class LiquidityBucket : uint8_t {
    HighlyLiquid = 0,    // Cash or convertible to cash within 3 business days without significantly changing market value
    ModeratelyLiquid = 1,// Convertible to cash in 4 to 7 calendar days
    LessLiquid = 2,      // Saleable in > 7 calendar days or maturity > 7 days
    Illiquid = 3,        // Cannot be sold/disposed of in 7 calendar days without significant price depression
    COUNT = 4
};

struct NPORTPositionRecord {
    char identifier[16]{0};        // CUSIP / SEDOL / ISIN / Ticker
    char issuer_name[48]{0};
    double market_value_usd{0.0};
    double daily_volume_usd{0.0};  // Average daily volume (ADV) in USD
    double bid_ask_spread_pct{0.0};// Bid-ask spread as % of mid price
    uint32_t days_to_settlement{2};
    bool is_restricted_security{false};
    LiquidityBucket assigned_bucket{LiquidityBucket::HighlyLiquid};
};

struct NPORTLiquiditySummary {
    double total_net_asset_value{0.0};
    double highly_liquid_pct{0.0};
    double moderately_liquid_pct{0.0};
    double less_liquid_pct{0.0};
    double illiquid_pct{0.0};
    bool illiquid_limit_breached{false}; // Rule 22e-4 strict 15% illiquid ceiling
    bool hlim_breached{false};           // Highly Liquid Investment Minimum breached
    size_t position_count{0};
};

struct FormNPORTConfig {
    double max_illiquid_pct_ceiling{15.0}; // Strict 15% statutory illiquid limit
    double highly_liquid_min_pct{50.0};    // HLIM target (e.g. 50%)
    double adv_participation_rate{0.10};  // Maximum assumed 10% ADV liquidation rate
};

class FormNPORTLiquidityEngine {
public:
    static constexpr size_t MAX_POSITIONS = 256;

    explicit FormNPORTLiquidityEngine(const FormNPORTConfig& cfg = FormNPORTConfig{}) noexcept
        : config_(cfg) {
        reset();
    }

    void reset() noexcept {
        position_count_ = 0;
    }

    // Classify individual position based on days required to liquidate 100% position at 10% ADV
    LiquidityBucket classify_position(const NPORTPositionRecord& pos) const noexcept {
        if (pos.is_restricted_security) {
            return LiquidityBucket::Illiquid;
        }

        if (pos.market_value_usd <= 0.0) {
            return LiquidityBucket::HighlyLiquid;
        }

        double daily_capacity = pos.daily_volume_usd * config_.adv_participation_rate;
        if (daily_capacity <= 0.0) {
            return LiquidityBucket::Illiquid;
        }

        double days_to_liquidate = pos.market_value_usd / daily_capacity;

        // Spread friction penalty
        if (pos.bid_ask_spread_pct > 0.05) { // > 5% spread indicates severe illiquidity
            days_to_liquidate *= 2.0;
        }

        if (days_to_liquidate <= 3.0) {
            return LiquidityBucket::HighlyLiquid;
        } else if (days_to_liquidate <= 7.0) {
            return LiquidityBucket::ModeratelyLiquid;
        } else if (days_to_liquidate <= 15.0) {
            return LiquidityBucket::LessLiquid;
        } else {
            return LiquidityBucket::Illiquid;
        }
    }

    bool add_position(NPORTPositionRecord pos) noexcept {
        if (position_count_ >= MAX_POSITIONS) return false;
        pos.assigned_bucket = classify_position(pos);
        positions_[position_count_++] = pos;
        return true;
    }

    NPORTLiquiditySummary generate_summary() const noexcept {
        NPORTLiquiditySummary summary{};
        summary.position_count = position_count_;

        double bucket_values[4]{0.0, 0.0, 0.0, 0.0};
        double total_nav = 0.0;

        for (size_t i = 0; i < position_count_; ++i) {
            const auto& p = positions_[i];
            total_nav += p.market_value_usd;
            size_t b_idx = static_cast<size_t>(p.assigned_bucket);
            if (b_idx < 4) {
                bucket_values[b_idx] += p.market_value_usd;
            }
        }

        summary.total_net_asset_value = total_nav;
        if (total_nav > 0.0) {
            summary.highly_liquid_pct = (bucket_values[0] / total_nav) * 100.0;
            summary.moderately_liquid_pct = (bucket_values[1] / total_nav) * 100.0;
            summary.less_liquid_pct = (bucket_values[2] / total_nav) * 100.0;
            summary.illiquid_pct = (bucket_values[3] / total_nav) * 100.0;
        }

        // Rule 22e-4 Compliance checks
        summary.illiquid_limit_breached = (summary.illiquid_pct > config_.max_illiquid_pct_ceiling);
        summary.hlim_breached = (summary.highly_liquid_pct < config_.highly_liquid_min_pct);

        return summary;
    }

    size_t position_count() const noexcept { return position_count_; }

private:
    FormNPORTConfig config_{};
    std::array<NPORTPositionRecord, MAX_POSITIONS> positions_{};
    size_t position_count_{0};
};

} // namespace luv
