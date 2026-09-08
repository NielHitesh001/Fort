#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

namespace luv {
namespace risk {

enum class MarginHealthState : uint8_t {
    kHealthy = 0,
    kWarning = 1,
    kMarginCallIssued = 2,
    kForcedLiquidationBreach = 3
};

struct MarginAccountState {
    uint32_t account_id = 0;
    int64_t total_equity = 0;             // Scaled x10,000
    int64_t initial_margin_req = 0;       // Scaled x10,000
    int64_t maintenance_margin_req = 0;   // Scaled x10,000
    uint64_t margin_call_timestamp_ns = 0;
    MarginHealthState state = MarginHealthState::kHealthy;
};

class MarginCallWatchdog {
public:
    static constexpr size_t kMaxAccounts = 64;
    static constexpr uint64_t kGracePeriodDurationNs = 3'600'000'000'000ULL; // 1 hour grace period

    MarginCallWatchdog() noexcept : num_accounts_(0) {}

    bool update_account(uint32_t account_id, int64_t equity, int64_t im_req, int64_t mm_req, uint64_t current_time_ns) noexcept {
        MarginAccountState* acc = nullptr;
        for (size_t i = 0; i < num_accounts_; ++i) {
            if (accounts_[i].account_id == account_id) {
                acc = &accounts_[i];
                break;
            }
        }

        if (!acc) {
            if (num_accounts_ >= kMaxAccounts) return false;
            acc = &accounts_[num_accounts_++];
            acc->account_id = account_id;
        }

        acc->total_equity = equity;
        acc->initial_margin_req = im_req;
        acc->maintenance_margin_req = mm_req;

        // Evaluate State Transition
        if (equity < mm_req) {
            // Equity dropped below Maintenance Margin -> Immediate forced liquidation
            acc->state = MarginHealthState::kForcedLiquidationBreach;
        } else if (equity < im_req) {
            // Equity dropped below Initial Margin -> Issue Margin Call
            if (acc->state != MarginHealthState::kMarginCallIssued) {
                acc->margin_call_timestamp_ns = current_time_ns;
                acc->state = MarginHealthState::kMarginCallIssued;
            } else {
                // Check if grace period expired
                if (current_time_ns - acc->margin_call_timestamp_ns > kGracePeriodDurationNs) {
                    acc->state = MarginHealthState::kForcedLiquidationBreach;
                }
            }
        } else {
            // Healthy
            acc->state = MarginHealthState::kHealthy;
            acc->margin_call_timestamp_ns = 0;
        }

        return true;
    }

    MarginHealthState get_account_state(uint32_t account_id) const noexcept {
        for (size_t i = 0; i < num_accounts_; ++i) {
            if (accounts_[i].account_id == account_id) {
                return accounts_[i].state;
            }
        }
        return MarginHealthState::kHealthy;
    }

private:
    std::array<MarginAccountState, kMaxAccounts> accounts_{};
    size_t num_accounts_{0};
};

} // namespace risk
} // namespace luv
