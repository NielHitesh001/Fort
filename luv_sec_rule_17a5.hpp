#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace luv {

enum class Rule17a5FilingType : uint8_t {
    AnnualAuditReport = 0,       // Form X-17A-5 Part III Annual Audited Financial Statements
    FocusReportPartII = 1,       // Carrying / Clearing Broker Focus Part II
    FocusReportPartIIA = 2,      // Introducing / Non-Carrying Broker Focus Part IIA
    CustomerReserveSchedule = 3, // Rule 15c3-3 Reserve Computation Exhibit A
    ExemptionReport = 4,         // Broker-Dealer Exemption Report (e.g. SEA Rule 15c3-3(k))
    ComplianceReport = 5         // Broker-Dealer Compliance Report (Carrying / Custody firms)
};

enum class AuditOpinionType : uint8_t {
    Unqualified = 0,             // Clean, unmodified PCAOB audit opinion
    Qualified = 1,               // Qualified audit opinion (material scope or GAAP departure)
    Adverse = 2,                 // Adverse opinion (financial statements not fairly presented)
    Disclaimer = 3               // Disclaimer of opinion (auditor unable to obtain sufficient evidence)
};

enum class BrokerDealerClassification : uint8_t {
    CarryingClearingBroker = 0,  // Custody and clearing (Rule 15c3-1 minimum $250,000 or debit items)
    IntroducingBroker = 1,       // Fully disclosed introducing broker (minimum $50,000)
    MarketMaker = 2,             // Dealer / Market Maker (minimum $100,000 to $1,000,000)
    ProprietaryTradingOnly = 3   // Proprietary trading broker-dealer (minimum $100,000)
};

struct Rule17a5FinancialSchedule {
    uint64_t total_assets_usd{0};
    uint64_t total_liabilities_usd{0};
    uint64_t allowable_subordinated_debt_usd{0};
    uint64_t non_allowable_assets_usd{0};
    uint64_t aggregate_indebtedness_usd{0};
    uint64_t aggregate_debit_items_usd{0};        // Rule 15c3-3 customer debit items
    uint64_t total_haircuts_usd{0};
    uint64_t undue_concentration_charges_usd{0};
    bool use_alternative_standard{false};        // 2% debit items instead of AI ratio
    bool is_first_year_operation{false};         // AI ratio limit 8:1 instead of 15:1
    uint64_t gross_securities_revenue_usd{0};     // For SIPC-7 assessment
    uint64_t sipc_allowable_deductions_usd{0};    // Permissible deductions from gross revenue
};

struct Rule17a5AuditVerification {
    uint64_t pcaob_firm_registration_id{0};
    AuditOpinionType audit_opinion{AuditOpinionType::Unqualified};
    bool has_material_weakness{false};
    bool has_internal_control_deficiencies{false};
    bool has_reconciliation_material_difference{false};
    bool exemption_provision_k1{false};          // Exemption under SEA 15c3-3(k)(1)
    bool exemption_provision_k2i{false};         // Exemption under SEA 15c3-3(k)(2)(i)
    bool exemption_provision_k2ii{false};        // Exemption under SEA 15c3-3(k)(2)(ii)
    bool non_covered_firm_footprint{false};      // SEC Footnote 74 Non-Covered Firm
};

struct Rule17a5AuditResult {
    int64_t net_worth_usd{0};
    int64_t tentative_net_capital_usd{0};
    int64_t net_capital_usd{0};
    uint64_t minimum_net_capital_required_usd{0};
    int64_t excess_net_capital_usd{0};
    double aggregate_indebtedness_ratio{0.0};
    double max_allowable_ai_ratio{15.0};
    bool is_net_capital_compliant{false};
    bool is_ai_ratio_compliant{false};
    bool is_rule_17a11_early_warning{false};           // NC < 120% min or AI > 12:1 (first year > 6.4:1)
    bool is_rule_17a11_critical_telegraphic_notice{false}; // NC < required minimum (immediate notice)
    uint64_t sipc_net_operating_revenue_usd{0};
    uint64_t sipc_assessment_fee_usd{0};              // General assessment @ 0.15% (15 bps)
    bool is_audit_filing_approved{false};
    uint32_t compliance_flags{0};                     // Bitmask of audit findings
};

class SecRule17a5AuditEngine {
public:
    static constexpr uint64_t kAbsoluteMinCarrying = 250'000ULL;
    static constexpr uint64_t kAbsoluteMinIntroducing = 50'000ULL;
    static constexpr uint64_t kAbsoluteMinMarketMaker = 100'000ULL;
    static constexpr uint64_t kAbsoluteMinPropTrading = 100'000ULL;
    static constexpr double kSipcAssessmentRate = 0.0015; // 0.15% (15 bps)

    static constexpr uint32_t FLAG_CLEAN_OPINION               = 0x0001;
    static constexpr uint32_t FLAG_NET_CAPITAL_PASS            = 0x0002;
    static constexpr uint32_t FLAG_AI_RATIO_PASS               = 0x0004;
    static constexpr uint32_t FLAG_SIPC_CALCULATED             = 0x0008;
    static constexpr uint32_t FLAG_EARLY_WARNING_ACTIVE        = 0x0010;
    static constexpr uint32_t FLAG_PCAOB_REGISTRATION_VALID    = 0x0020;
    static constexpr uint32_t FLAG_MATERIAL_WEAKNESS_NOTED     = 0x0040;
    static constexpr uint32_t FLAG_TELEGRAPHIC_NOTICE_REQUIRED = 0x0080;

    static Rule17a5AuditResult evaluate_filing(
        const Rule17a5FinancialSchedule& sched,
        const Rule17a5AuditVerification& audit,
        BrokerDealerClassification classification = BrokerDealerClassification::CarryingClearingBroker) noexcept
    {
        Rule17a5AuditResult res{};

        // 1. Net Worth = Total Assets - Total Liabilities
        res.net_worth_usd = static_cast<int64_t>(sched.total_assets_usd) - static_cast<int64_t>(sched.total_liabilities_usd);

        // 2. Tentative Net Capital = Net Worth + Allowable Subordinated Debt - Non-Allowable Assets
        int64_t capital_base = res.net_worth_usd + static_cast<int64_t>(sched.allowable_subordinated_debt_usd);
        res.tentative_net_capital_usd = capital_base - static_cast<int64_t>(sched.non_allowable_assets_usd);

        // 3. Net Capital = Tentative Net Capital - Haircuts - Undue Concentration Charges
        res.net_capital_usd = res.tentative_net_capital_usd - 
                              static_cast<int64_t>(sched.total_haircuts_usd) - 
                              static_cast<int64_t>(sched.undue_concentration_charges_usd);

        // 4. Base Minimum Capital requirement by Broker-Dealer classification
        uint64_t base_min_capital = kAbsoluteMinCarrying;
        switch (classification) {
            case BrokerDealerClassification::CarryingClearingBroker:
                base_min_capital = kAbsoluteMinCarrying;
                break;
            case BrokerDealerClassification::IntroducingBroker:
                base_min_capital = kAbsoluteMinIntroducing;
                break;
            case BrokerDealerClassification::MarketMaker:
                base_min_capital = kAbsoluteMinMarketMaker;
                break;
            case BrokerDealerClassification::ProprietaryTradingOnly:
                base_min_capital = kAbsoluteMinPropTrading;
                break;
        }

        // 5. Compute Rule 15c3-1 Required Minimum Net Capital
        if (sched.use_alternative_standard) {
            // Alternative Standard: max(Base Min, 2% of Rule 15c3-3 debit items)
            uint64_t debit_requirement = static_cast<uint64_t>(std::round(sched.aggregate_debit_items_usd * 0.02));
            res.minimum_net_capital_required_usd = std::max(base_min_capital, debit_requirement);
            res.max_allowable_ai_ratio = 0.0; // AI ratio is not applicable under Alternative Standard
            res.is_ai_ratio_compliant = true;
            res.aggregate_indebtedness_ratio = 0.0;
        } else {
            // Aggregate Indebtedness Standard:
            // For established BD: min is max(Base Min, 6.67% of AI) [15:1 ratio]
            // For first-year BD: min is max(Base Min, 12.5% of AI) [8:1 ratio]
            res.max_allowable_ai_ratio = sched.is_first_year_operation ? 8.0 : 15.0;
            double ai_fraction = sched.is_first_year_operation ? 0.125 : (1.0 / 15.0);
            uint64_t ai_min = static_cast<uint64_t>(std::ceil(sched.aggregate_indebtedness_usd * ai_fraction));
            res.minimum_net_capital_required_usd = std::max(base_min_capital, ai_min);

            if (res.net_capital_usd > 0) {
                res.aggregate_indebtedness_ratio = static_cast<double>(sched.aggregate_indebtedness_usd) / 
                                                  static_cast<double>(res.net_capital_usd);
                res.is_ai_ratio_compliant = (res.aggregate_indebtedness_ratio <= res.max_allowable_ai_ratio);
            } else {
                res.aggregate_indebtedness_ratio = (sched.aggregate_indebtedness_usd > 0) ? 999.99 : 0.0;
                res.is_ai_ratio_compliant = (sched.aggregate_indebtedness_usd == 0);
            }
        }

        // 6. Excess Net Capital
        res.excess_net_capital_usd = res.net_capital_usd - static_cast<int64_t>(res.minimum_net_capital_required_usd);
        res.is_net_capital_compliant = (res.excess_net_capital_usd >= 0);

        // 7. Rule 17a-11 Early Warning & Telegraphic Notice Thresholds
        uint64_t early_warning_threshold = static_cast<uint64_t>(res.minimum_net_capital_required_usd * 1.20);
        double early_warning_ai_limit = sched.is_first_year_operation ? 6.4 : 12.0;

        if (res.net_capital_usd < static_cast<int64_t>(early_warning_threshold)) {
            res.is_rule_17a11_early_warning = true;
        }
        if (!sched.use_alternative_standard && res.aggregate_indebtedness_ratio > early_warning_ai_limit) {
            res.is_rule_17a11_early_warning = true;
        }

        if (res.net_capital_usd < static_cast<int64_t>(res.minimum_net_capital_required_usd)) {
            res.is_rule_17a11_critical_telegraphic_notice = true;
        }

        // 8. SIPC-7 General Assessment Calculation
        if (sched.gross_securities_revenue_usd > sched.sipc_allowable_deductions_usd) {
            res.sipc_net_operating_revenue_usd = sched.gross_securities_revenue_usd - sched.sipc_allowable_deductions_usd;
        } else {
            res.sipc_net_operating_revenue_usd = 0;
        }
        res.sipc_assessment_fee_usd = static_cast<uint64_t>(std::round(res.sipc_net_operating_revenue_usd * kSipcAssessmentRate));

        // 9. Compliance Bitmask and Filing Approval
        if (audit.audit_opinion == AuditOpinionType::Unqualified) {
            res.compliance_flags |= FLAG_CLEAN_OPINION;
        }
        if (audit.pcaob_firm_registration_id > 0) {
            res.compliance_flags |= FLAG_PCAOB_REGISTRATION_VALID;
        }
        if (res.is_net_capital_compliant) {
            res.compliance_flags |= FLAG_NET_CAPITAL_PASS;
        }
        if (res.is_ai_ratio_compliant) {
            res.compliance_flags |= FLAG_AI_RATIO_PASS;
        }
        res.compliance_flags |= FLAG_SIPC_CALCULATED;

        if (res.is_rule_17a11_early_warning) {
            res.compliance_flags |= FLAG_EARLY_WARNING_ACTIVE;
        }
        if (res.is_rule_17a11_critical_telegraphic_notice) {
            res.compliance_flags |= FLAG_TELEGRAPHIC_NOTICE_REQUIRED;
        }
        if (audit.has_material_weakness) {
            res.compliance_flags |= FLAG_MATERIAL_WEAKNESS_NOTED;
        }

        // Approved if clean/acceptable opinion, valid PCAOB ID, compliant net capital & AI ratio, and no critical telegraphic deficiency
        res.is_audit_filing_approved = (audit.pcaob_firm_registration_id > 0) &&
                                       (audit.audit_opinion == AuditOpinionType::Unqualified) &&
                                       res.is_net_capital_compliant &&
                                       res.is_ai_ratio_compliant &&
                                       !audit.has_reconciliation_material_difference &&
                                       !res.is_rule_17a11_critical_telegraphic_notice;

        return res;
    }
};

} // namespace luv
