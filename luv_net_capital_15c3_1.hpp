#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace luv {

enum class NetCapitalStandard : uint8_t {
    AggregateIndebtedness = 0, // AI Standard: Min Net Capital = max($250k, 6.67% of AI)
    AlternativeStandard = 1    // Alt Standard: Min Net Capital = max($250k, 2.00% of debit items)
};

struct NetCapitalPosition {
    uint64_t symbol_id{0};
    uint64_t market_value_usd{0}; // Scaled in full USD
    bool is_equity{true};
    bool is_treasury{false};
    uint32_t treasury_maturity_years{0}; // For maturity-based haircut
    bool is_non_marketable{false};       // 100% deduction
};

struct NetCapitalResult {
    uint64_t allowable_assets{0};
    uint64_t total_deductions{0};
    uint64_t tentative_net_capital{0};
    uint64_t total_haircuts{0};
    uint64_t net_capital{0};
    uint64_t required_minimum_net_capital{0};
    uint64_t excess_net_capital{0};
    bool is_compliant{true};
    bool is_early_warning{false}; // Net Capital < 120% of minimum requirement
};

class NetCapitalCalculator {
public:
    static constexpr uint64_t kAbsoluteMinimum = 250'000ULL; // $250,000 minimum broker-dealer capital

    static NetCapitalResult calculate(
        uint64_t total_assets,
        uint64_t non_allowable_assets,
        uint64_t aggregate_indebtedness_or_debits,
        const NetCapitalPosition* positions,
        size_t position_count,
        NetCapitalStandard standard = NetCapitalStandard::AggregateIndebtedness) noexcept
    {
        NetCapitalResult res;

        // 1. Tentative Net Capital = Total Assets - Non-Allowable Assets (100% deduction for illiquid/unsecured items)
        res.allowable_assets = (total_assets > non_allowable_assets) ? (total_assets - non_allowable_assets) : 0;
        res.total_deductions = non_allowable_assets;
        res.tentative_net_capital = res.allowable_assets;

        // 2. Compute Security Haircuts (15c3-1(c)(2)(vi))
        uint64_t haircuts = 0;
        for (size_t i = 0; i < position_count; ++i) {
            const auto& pos = positions[i];
            uint64_t val = pos.market_value_usd;

            if (pos.is_non_marketable) {
                haircuts += val; // 100% deduction
            } else if (pos.is_equity) {
                // Standard 15% haircut on equities
                uint64_t hc = (val * 15) / 100;

                // Undue concentration charge: additional 15% on amount exceeding 10% of tentative net capital
                uint64_t ten_pct_tnc = res.tentative_net_capital / 10;
                if (val > ten_pct_tnc && ten_pct_tnc > 0) {
                    uint64_t excess = val - ten_pct_tnc;
                    hc += (excess * 15) / 100;
                }
                haircuts += hc;
            } else if (pos.is_treasury) {
                // Treasury haircut graduated by maturity: <1yr: 0%, 1-3yr: 2%, 3-5yr: 3%, >5yr: 6%
                uint64_t rate_pct = 0;
                if (pos.treasury_maturity_years >= 5) rate_pct = 6;
                else if (pos.treasury_maturity_years >= 3) rate_pct = 3;
                else if (pos.treasury_maturity_years >= 1) rate_pct = 2;
                else rate_pct = 0;

                haircuts += (val * rate_pct) / 100;
            }
        }

        res.total_haircuts = haircuts;
        res.net_capital = (res.tentative_net_capital > haircuts) ? (res.tentative_net_capital - haircuts) : 0;

        // 3. Minimum Net Capital Requirement
        uint64_t ratio_requirement = 0;
        if (standard == NetCapitalStandard::AggregateIndebtedness) {
            // 6 2/3% (approx 1/15th) of Aggregate Indebtedness
            ratio_requirement = (aggregate_indebtedness_or_debits * 2) / 30; // equivalent to 6.666%
        } else {
            // 2% of debit items
            ratio_requirement = (aggregate_indebtedness_or_debits * 2) / 100;
        }

        res.required_minimum_net_capital = std::max(kAbsoluteMinimum, ratio_requirement);

        // 4. Compliance and Early Warning (Rule 17a-11: early warning is 120% of min required)
        res.is_compliant = (res.net_capital >= res.required_minimum_net_capital);
        res.excess_net_capital = res.is_compliant ? (res.net_capital - res.required_minimum_net_capital) : 0;

        uint64_t early_warning_thresh = (res.required_minimum_net_capital * 120) / 100;
        res.is_early_warning = (res.net_capital < early_warning_thresh);

        return res;
    }
};

} // namespace luv
