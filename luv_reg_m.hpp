#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace compliance {

enum class RegMRestrictedReason : uint8_t {
    kNone = 0,
    kOfferingDistributionActive = 1,
    kRestrictedPeriodBiddingProhibited = 2,
    kSyndicateCoveringBidExceeded = 3
};

struct RegMDistributionOffering {
    uint16_t symbol_idx = 0;
    uint32_t participant_mpid = 0;      // MPID of distribution participant
    uint64_t restricted_period_start_ns = 0;
    uint64_t pricing_date_ns = 0;
    int64_t offering_price = 0;         // Scaled x10,000
    bool is_actively_traded_exempt = false; // ADTV >= $1M and float >= $150M exempt from Rule 101
};

class RegMValidator {
public:
    static constexpr size_t kMaxOfferings = 32;

    RegMValidator() noexcept : num_offerings_(0) {}

    bool register_offering(const RegMDistributionOffering& offering) noexcept {
        if (num_offerings_ >= kMaxOfferings) return false;
        offerings_[num_offerings_++] = offering;
        return true;
    }

    const RegMDistributionOffering* find_offering(uint16_t sym) const noexcept {
        for (size_t i = 0; i < num_offerings_; ++i) {
            if (offerings_[i].symbol_idx == sym) return &offerings_[i];
        }
        return nullptr;
    }

    // Evaluates order against SEC Regulation M (Rules 101 & 102)
    RegMRestrictedReason validate_order(uint16_t sym, uint32_t mpid, uint8_t side, int64_t price, uint64_t current_time_ns) const noexcept {
        const auto* off = find_offering(sym);
        if (!off) return RegMRestrictedReason::kNone; // No offering in progress

        // If participant is part of the distribution syndicate
        if (off->participant_mpid != 0 && off->participant_mpid != mpid) {
            return RegMRestrictedReason::kNone; // Unaffiliated participant
        }

        if (off->is_actively_traded_exempt) {
            return RegMRestrictedReason::kNone; // Rule 101 exemption applies
        }

        // Check if currently inside Restricted Period (between start and pricing)
        if (current_time_ns >= off->restricted_period_start_ns && current_time_ns <= off->pricing_date_ns) {
            // Rule 101/102: Bidding for or purchasing (Buy side) is strictly prohibited
            if (side == exec::kBuy) {
                return RegMRestrictedReason::kRestrictedPeriodBiddingProhibited;
            }
        }

        return RegMRestrictedReason::kNone;
    }

private:
    std::array<RegMDistributionOffering, kMaxOfferings> offerings_{};
    size_t num_offerings_{0};
};

} // namespace compliance
} // namespace luv
