#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace luv {

enum class InsiderRole : uint8_t {
    DirectorOrOfficer = 0, // Requires max(90 days, 2 business days after Form 10-Q/10-K filing) up to 120 days
    OtherInsider = 1       // Requires 30 days cooling-off period
};

struct TradingPlan10b51 {
    uint64_t plan_id{0};
    uint64_t insider_id{0};
    InsiderRole role{InsiderRole::DirectorOrOfficer};
    uint64_t plan_adoption_time_ns{0};
    uint64_t cooling_off_period_ns{0};
    bool is_single_trade_plan{false};
    uint64_t last_single_trade_plan_ns{0}; // Enforces 1 single-trade plan per 12 months
    bool is_active{true};
    bool is_modified{false};
    uint64_t modification_time_ns{0};
};

struct PlannedTradeOrder {
    uint64_t symbol_id{0};
    bool is_buy{false};
    uint64_t min_price{0};
    uint64_t max_price{0};
    uint64_t target_quantity{0};
};

class Plan10b51Validator {
public:
    static constexpr uint64_t kSecInNs = 1'000'000'000ULL;
    static constexpr uint64_t kDayInNs = 86'400ULL * kSecInNs;
    static constexpr uint64_t kYearInNs = 365ULL * kDayInNs;

    static constexpr uint64_t kDirectorCoolingOffNs = 90ULL * kDayInNs; // 90 days
    static constexpr uint64_t kOtherCoolingOffNs = 30ULL * kDayInNs;    // 30 days

    // Validates whether an order execution is permitted under SEC Rule 10b5-1 affirmative defense
    static bool is_trade_permitted(
        const TradingPlan10b51& plan,
        uint64_t current_time_ns,
        const PlannedTradeOrder& order,
        uint64_t trade_price,
        uint64_t trade_quantity) noexcept
    {
        if (!plan.is_active) return false;

        // 1. Check cooling-off period from adoption or modification
        uint64_t base_time = plan.is_modified ? plan.modification_time_ns : plan.plan_adoption_time_ns;
        uint64_t req_cooling = (plan.role == InsiderRole::DirectorOrOfficer) ? kDirectorCoolingOffNs : kOtherCoolingOffNs;

        if (current_time_ns < base_time + req_cooling) {
            return false; // In cooling-off period, execution not permitted
        }

        // 2. Check single-trade plan 12-month restriction
        if (plan.is_single_trade_plan) {
            if (plan.last_single_trade_plan_ns > 0 && current_time_ns < plan.last_single_trade_plan_ns + kYearInNs) {
                return false; // Exceeds 1 single-trade plan per 12 months
            }
        }

        // 3. Adherence to formula parameters
        if (order.min_price > 0 && trade_price < order.min_price) return false;
        if (order.max_price > 0 && trade_price > order.max_price) return false;
        if (trade_quantity > order.target_quantity) return false;

        return true;
    }
};

} // namespace luv
