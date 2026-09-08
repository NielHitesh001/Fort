#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <algorithm>
#include <string_view>

namespace luv {

enum class IssuerReportingStatus : uint8_t {
    SECReportingIssuer = 0,    // Subject to Exchange Act reporting >= 90 days
    NonReportingIssuer = 1
};

enum class SellerAffiliateStatus : uint8_t {
    Affiliate = 0,             // Control person / officer / director / >10% shareholder
    NonAffiliate = 1           // Non-control person
};

struct Rule144SecurityHolding {
    char symbol[12]{0};
    char shareholder_id[24]{0};
    SellerAffiliateStatus affiliate_status{SellerAffiliateStatus::NonAffiliate};
    IssuerReportingStatus issuer_status{IssuerReportingStatus::SECReportingIssuer};
    uint64_t acquisition_timestamp_ns{0}; // Date restricted securities were acquired and fully paid
    uint64_t total_restricted_shares{0};
    uint64_t total_shares_outstanding{0};
    uint64_t four_week_avg_weekly_volume{0}; // 4-week ADTV
    uint64_t shares_sold_last_3_months{0};
    double dollar_value_sold_last_3_months{0.0};
    bool form_144_filed{false};
    bool issuer_filings_current{true};   // Current public info requirement
};

struct Rule144ValidationResult {
    bool sale_permitted{false};
    uint64_t max_permitted_shares{0};
    bool form_144_required{false};
    bool volume_limit_breached{false};
    bool holding_period_breached{false};
    char violation_reason[64]{0};
};

class SECRule144Engine {
public:
    static constexpr uint64_t SIX_MONTHS_NS = 180ULL * 24 * 3600 * 1'000'000'000ULL;
    static constexpr uint64_t ONE_YEAR_NS = 365ULL * 24 * 3600 * 1'000'000'000ULL;
    static constexpr uint64_t FORM_144_SHARE_THRESHOLD = 5000;
    static constexpr double FORM_144_DOLLAR_THRESHOLD = 50'000.0;

    static Rule144ValidationResult validate_sale(
        const Rule144SecurityHolding& holding,
        uint64_t proposed_shares,
        double proposed_price,
        uint64_t current_time_ns) noexcept {

        Rule144ValidationResult res{};

        // 1. Holding Period Check under Rule 144(d)
        uint64_t holding_duration = (current_time_ns > holding.acquisition_timestamp_ns) 
            ? (current_time_ns - holding.acquisition_timestamp_ns) : 0;

        uint64_t required_holding = (holding.issuer_status == IssuerReportingStatus::SECReportingIssuer) 
            ? SIX_MONTHS_NS : ONE_YEAR_NS;

        if (holding_duration < required_holding) {
            res.sale_permitted = false;
            res.holding_period_breached = true;
            std::strncpy(res.violation_reason, "HOLDING_PERIOD_NOT_MET", sizeof(res.violation_reason) - 1);
            return res;
        }

        // 2. Current Public Information under Rule 144(c)
        if (!holding.issuer_filings_current) {
            res.sale_permitted = false;
            std::strncpy(res.violation_reason, "ISSUER_PUBLIC_INFO_NOT_CURRENT", sizeof(res.violation_reason) - 1);
            return res;
        }

        // If Non-Affiliate holding > 1 year for reporting issuer, unrestricted public resale
        if (holding.affiliate_status == SellerAffiliateStatus::NonAffiliate && holding_duration >= ONE_YEAR_NS) {
            res.sale_permitted = true;
            res.max_permitted_shares = holding.total_restricted_shares;
            res.form_144_required = false;
            return res;
        }

        // 3. Volume Limitation for Affiliates under Rule 144(e)
        // Greater of 1% of shares outstanding or average weekly trading volume for 4 preceding weeks
        uint64_t one_pct_outstanding = holding.total_shares_outstanding / 100;
        uint64_t weekly_volume = holding.four_week_avg_weekly_volume;
        uint64_t volume_ceiling = std::max(one_pct_outstanding, weekly_volume);

        uint64_t already_sold = holding.shares_sold_last_3_months;
        uint64_t remaining_capacity = (volume_ceiling > already_sold) ? (volume_ceiling - already_sold) : 0;

        if (proposed_shares > remaining_capacity) {
            res.sale_permitted = false;
            res.volume_limit_breached = true;
            res.max_permitted_shares = remaining_capacity;
            std::strncpy(res.violation_reason, "RULE_144_VOLUME_LIMIT_EXCEEDED", sizeof(res.violation_reason) - 1);
            return res;
        }

        // 4. Form 144 Notice Filing Check under Rule 144(h)
        // Required if proposed order + last 3 months sales > 5,000 shares or > $50,000
        uint64_t total_3m_shares = already_sold + proposed_shares;
        double total_3m_dollars = holding.dollar_value_sold_last_3_months + (static_cast<double>(proposed_shares) * proposed_price);

        if (total_3m_shares > FORM_144_SHARE_THRESHOLD || total_3m_dollars > FORM_144_DOLLAR_THRESHOLD) {
            res.form_144_required = true;
            if (holding.affiliate_status == SellerAffiliateStatus::Affiliate && !holding.form_144_filed) {
                res.sale_permitted = false;
                std::strncpy(res.violation_reason, "FORM_144_FILING_REQUIRED", sizeof(res.violation_reason) - 1);
                return res;
            }
        }

        res.sale_permitted = true;
        res.max_permitted_shares = remaining_capacity;
        return res;
    }
};

} // namespace luv
