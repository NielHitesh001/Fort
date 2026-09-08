#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "luv_execution.hpp"

namespace luv {
namespace ats_n {

enum class SubscriberTier : uint8_t {
    kRetail = 0,
    kInstitutional = 1,
    kMarketMaker = 2,
    kBrokerDealerAffiliate = 3
};

enum class PoolSegmentation : uint8_t {
    kPublicAll = 0,
    kInstitutionalOnly = 1,
    kRetailOnly = 2,
    kMarketMakerNeutral = 3
};

struct SubscriberProfile {
    uint32_t subscriber_id = 0;
    SubscriberTier tier = SubscriberTier::kInstitutional;
    bool opt_out_internalization = false;
    bool opt_out_retail_interaction = false;
};

class AtsTransparencyEngine {
public:
    static constexpr size_t kMaxSubscribers = 128;

    AtsTransparencyEngine() noexcept : num_subs_(0) {}

    bool register_subscriber(const SubscriberProfile& profile) noexcept {
        if (num_subs_ >= kMaxSubscribers) return false;
        subscribers_[num_subs_++] = profile;
        return true;
    }

    const SubscriberProfile* find_subscriber(uint32_t sub_id) const noexcept {
        for (size_t i = 0; i < num_subs_; ++i) {
            if (subscribers_[i].subscriber_id == sub_id) {
                return &subscribers_[i];
            }
        }
        return nullptr;
    }

    // Evaluates whether Maker and Taker are permitted to interact under ATS-N disclosures
    bool can_interact(uint32_t maker_sub_id, uint32_t taker_sub_id, PoolSegmentation pool_type) const noexcept {
        const auto* maker = find_subscriber(maker_sub_id);
        const auto* taker = find_subscriber(taker_sub_id);

        if (!maker || !taker) return false;

        // Same subscriber self-interaction check (if opted out)
        if (maker->subscriber_id == taker->subscriber_id && (maker->opt_out_internalization || taker->opt_out_internalization)) {
            return false;
        }

        // Retail interaction opt-out
        if (maker->opt_out_retail_interaction && taker->tier == SubscriberTier::kRetail) {
            return false;
        }
        if (taker->opt_out_retail_interaction && maker->tier == SubscriberTier::kRetail) {
            return false;
        }

        // Pool segmentation rules
        if (pool_type == PoolSegmentation::kInstitutionalOnly) {
            if (maker->tier != SubscriberTier::kInstitutional || taker->tier != SubscriberTier::kInstitutional) {
                return false;
            }
        } else if (pool_type == PoolSegmentation::kRetailOnly) {
            if (maker->tier != SubscriberTier::kRetail && taker->tier != SubscriberTier::kRetail) {
                return false;
            }
        }

        return true;
    }

private:
    std::array<SubscriberProfile, kMaxSubscribers> subscribers_{};
    size_t num_subs_{0};
};

} // namespace ats_n
} // namespace luv
