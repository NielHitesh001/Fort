#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace luv {

enum class RiskCheckResult : uint8_t {
    Approved = 0,
    ExceedsMaxNotional = 1,
    FatFingerPriceCollarBreach = 2,
    CreditLimitExceeded = 3,
    OrderRateVelocityBreach = 4,
    KillSwitchActive = 5,
    IntegerOverflowDetected = 6,
    InvalidPriceOrQuantity = 7
};

struct PreTradeRiskConfig {
    uint64_t max_single_order_notional_usd{1'000'000ULL}; // $1,000,000 max per order
    uint64_t max_cumulative_notional_usd{10'000'000ULL};  // $10,000,000 max open credit
    double max_price_collar_fraction{0.05};               // 5% max deviation from BBO
    uint32_t max_orders_per_second{500};                  // Rate velocity limit
};

class PreTradeRiskSecurityFirewall {
public:
    explicit PreTradeRiskSecurityFirewall(const PreTradeRiskConfig& config) noexcept
        : config_(config), current_gross_notional_usd_(0), order_count_in_window_(0),
          window_start_ns_(0), kill_switch_locked_(false) {}

    void trigger_emergency_kill_switch() noexcept {
        kill_switch_locked_ = true;
    }

    void reset_kill_switch() noexcept {
        kill_switch_locked_ = false;
    }

    bool is_kill_switch_active() const noexcept {
        return kill_switch_locked_;
    }

    RiskCheckResult validate_order(
        uint64_t order_id,
        bool is_buy,
        uint64_t price_cents,
        uint64_t qty,
        uint64_t bbo_bid_cents,
        uint64_t bbo_ask_cents,
        uint64_t now_ns) noexcept
    {
        (void)order_id;
        (void)is_buy;

        // 1. Kill Switch Check
        if (kill_switch_locked_) {
            return RiskCheckResult::KillSwitchActive;
        }

        // 2. Basic non-zero validation
        if (price_cents == 0 || qty == 0) {
            return RiskCheckResult::InvalidPriceOrQuantity;
        }

        // 3. Integer Overflow Safe Notional Calculation (price_cents * qty)
        uint64_t notional_cents = 0;
        if (__builtin_mul_overflow(price_cents, qty, &notional_cents)) {
            return RiskCheckResult::IntegerOverflowDetected;
        }
        uint64_t notional_usd = notional_cents / 100ULL;

        // 4. Single-Order Max Notional Limit
        if (notional_usd > config_.max_single_order_notional_usd) {
            return RiskCheckResult::ExceedsMaxNotional;
        }

        // 5. Fat-Finger Price Collar Check vs BBO
        uint64_t ref_price = (bbo_bid_cents + bbo_ask_cents) / 2;
        if (ref_price > 0) {
            double price_diff = std::abs(static_cast<double>(price_cents) - static_cast<double>(ref_price));
            double max_allowed_diff = static_cast<double>(ref_price) * config_.max_price_collar_fraction;
            if (price_diff > max_allowed_diff) {
                return RiskCheckResult::FatFingerPriceCollarBreach;
            }
        }

        // 6. Cumulative Gross Notional Credit Limit
        uint64_t next_gross = 0;
        if (__builtin_add_overflow(current_gross_notional_usd_, notional_usd, &next_gross) ||
            next_gross > config_.max_cumulative_notional_usd) 
        {
            return RiskCheckResult::CreditLimitExceeded;
        }

        // 7. Order Velocity Limiter
        if (window_start_ns_ == 0 || (now_ns - window_start_ns_) >= 1'000'000'000ULL) {
            window_start_ns_ = now_ns;
            order_count_in_window_ = 1;
        } else {
            ++order_count_in_window_;
            if (order_count_in_window_ > config_.max_orders_per_second) {
                return RiskCheckResult::OrderRateVelocityBreach;
            }
        }

        // Commit open notional
        current_gross_notional_usd_ = next_gross;
        return RiskCheckResult::Approved;
    }

    void on_order_fill_or_cancel(uint64_t price_cents, uint64_t qty) noexcept {
        uint64_t notional_usd = (price_cents * qty) / 100ULL;
        if (current_gross_notional_usd_ >= notional_usd) {
            current_gross_notional_usd_ -= notional_usd;
        } else {
            current_gross_notional_usd_ = 0;
        }
    }

    uint64_t current_gross_notional() const noexcept {
        return current_gross_notional_usd_;
    }

private:
    PreTradeRiskConfig config_{};
    uint64_t current_gross_notional_usd_{0};
    uint32_t order_count_in_window_{0};
    uint64_t window_start_ns_{0};
    bool kill_switch_locked_{false};
};

} // namespace luv
