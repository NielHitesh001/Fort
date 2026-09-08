#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace compliance {

enum class Rule10b18RejectReason : uint8_t {
    kNone = 0,
    kMultipleBrokersViolated = 1,
    kTimingConditionViolated = 2,
    kPriceConditionViolated = 3,
    kVolumeCapExceeded = 4
};

struct Rule10b18DailyProfile {
    uint16_t symbol_idx = 0;
    uint32_t authorized_broker_id = 0;
    int64_t four_week_adtv = 0;             // Average Daily Trading Volume
    int64_t accumulated_repurchase_qty = 0; // Cumulative buyback today
    uint64_t market_open_ns = 0;
    uint64_t market_close_ns = 0;
};

class Rule10b18SafeHarborEngine {
public:
    static constexpr size_t kMaxSymbols = 32;
    static constexpr double kMaxVolumePctAdtv = 0.25; // 25% ADTV cap

    Rule10b18SafeHarborEngine() noexcept : num_symbols_(0) {}

    bool register_symbol(const Rule10b18DailyProfile& profile) noexcept {
        if (num_symbols_ >= kMaxSymbols || profile.four_week_adtv <= 0) return false;
        profiles_[num_symbols_++] = profile;
        return true;
    }

    Rule10b18DailyProfile* find_profile(uint16_t sym) noexcept {
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (profiles_[i].symbol_idx == sym) return &profiles_[i];
        }
        return nullptr;
    }

    // Validates if a proposed corporate buyback order complies with SEC Rule 10b-18 Safe Harbor
    Rule10b18RejectReason validate_buyback_order(uint16_t sym, uint32_t broker_id, int64_t price, int64_t qty,
                                                  int64_t highest_independent_bid, int64_t last_sale_price,
                                                  uint64_t current_time_ns) noexcept {
        auto* prof = find_profile(sym);
        if (!prof) return Rule10b18RejectReason::kNone; // Not a registered 10b-18 buyback symbol

        // 1. Single broker-dealer condition
        if (prof->authorized_broker_id != 0 && prof->authorized_broker_id != broker_id) {
            return Rule10b18RejectReason::kMultipleBrokersViolated;
        }

        // 2. Timing condition: Cannot execute in first 10 minutes or last 10 minutes of normal session
        // (10 mins = 600,000,000,000 ns)
        constexpr uint64_t kTenMinutesNs = 600'000'000'000ULL;
        if (current_time_ns < prof->market_open_ns + kTenMinutesNs ||
            current_time_ns > prof->market_close_ns - kTenMinutesNs) {
            return Rule10b18RejectReason::kTimingConditionViolated;
        }

        // 3. Price condition: Cannot exceed the higher of highest independent bid and last sale price
        int64_t max_allowed_price = std::max(highest_independent_bid, last_sale_price);
        if (max_allowed_price > 0 && price > max_allowed_price) {
            return Rule10b18RejectReason::kPriceConditionViolated;
        }

        // 4. Volume condition: Daily total cannot exceed 25% of 4-week ADTV
        int64_t max_daily_qty = static_cast<int64_t>(prof->four_week_adtv * kMaxVolumePctAdtv);
        if (prof->accumulated_repurchase_qty + qty > max_daily_qty) {
            return Rule10b18RejectReason::kVolumeCapExceeded;
        }

        // Compliant: Reserve volume
        prof->accumulated_repurchase_qty += qty;
        return Rule10b18RejectReason::kNone;
    }

private:
    std::array<Rule10b18DailyProfile, kMaxSymbols> profiles_{};
    size_t num_symbols_{0};
};

} // namespace compliance
} // namespace luv
