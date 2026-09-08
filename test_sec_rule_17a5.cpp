#include "luv_sec_rule_17a5.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_carrying_bd_compliant_ai_standard() {
    Rule17a5FinancialSchedule sched{};
    sched.total_assets_usd = 50'000'000ULL;
    sched.total_liabilities_usd = 30'000'000ULL;
    sched.allowable_subordinated_debt_usd = 5'000'000ULL;
    sched.non_allowable_assets_usd = 2'000'000ULL;
    sched.aggregate_indebtedness_usd = 15'000'000ULL;
    sched.total_haircuts_usd = 1'500'000ULL;
    sched.undue_concentration_charges_usd = 500'000ULL;
    sched.use_alternative_standard = false;
    sched.is_first_year_operation = false;
    sched.gross_securities_revenue_usd = 10'000'000ULL;
    sched.sipc_allowable_deductions_usd = 2'000'000ULL;

    Rule17a5AuditVerification audit{};
    audit.pcaob_firm_registration_id = 12345;
    audit.audit_opinion = AuditOpinionType::Unqualified;
    audit.has_material_weakness = false;
    audit.has_internal_control_deficiencies = false;
    audit.has_reconciliation_material_difference = false;

    auto res = SecRule17a5AuditEngine::evaluate_filing(sched, audit, BrokerDealerClassification::CarryingClearingBroker);

    // Net worth = 50M - 30M = 20M
    assert(res.net_worth_usd == 20'000'000LL);
    // Tentative net capital = 20M + 5M - 2M = 23M
    assert(res.tentative_net_capital_usd == 23'000'000LL);
    // Net capital = 23M - 1.5M - 0.5M = 21M
    assert(res.net_capital_usd == 21'000'000LL);

    // Required min: max(250k, 15M / 15 = 1M) -> 1,000,000
    assert(res.minimum_net_capital_required_usd == 1'000'000ULL);
    assert(res.excess_net_capital_usd == 20'000'000LL);
    assert(res.is_net_capital_compliant == true);
    assert(res.aggregate_indebtedness_ratio < 1.0); // 15M / 21M ~ 0.714
    assert(res.is_ai_ratio_compliant == true);
    assert(res.is_rule_17a11_early_warning == false);
    assert(res.is_rule_17a11_critical_telegraphic_notice == false);

    // SIPC assessment: (10M - 2M) * 0.0015 = 8M * 0.0015 = 12,000
    assert(res.sipc_net_operating_revenue_usd == 8'000'000ULL);
    assert(res.sipc_assessment_fee_usd == 12'000ULL);

    assert(res.is_audit_filing_approved == true);
}

void test_first_year_and_early_warning_triggers() {
    Rule17a5FinancialSchedule sched{};
    sched.total_assets_usd = 5'000'000ULL;
    sched.total_liabilities_usd = 4'000'000ULL;
    sched.allowable_subordinated_debt_usd = 0;
    sched.non_allowable_assets_usd = 500'000ULL;
    sched.aggregate_indebtedness_usd = 3'500'000ULL;
    sched.total_haircuts_usd = 100'000ULL;
    sched.undue_concentration_charges_usd = 0;
    sched.use_alternative_standard = false;
    sched.is_first_year_operation = true; // 8:1 max AI ratio

    Rule17a5AuditVerification audit{};
    audit.pcaob_firm_registration_id = 999;
    audit.audit_opinion = AuditOpinionType::Unqualified;

    auto res = SecRule17a5AuditEngine::evaluate_filing(sched, audit, BrokerDealerClassification::IntroducingBroker);

    // Net Worth = 5M - 4M = 1M
    // Tentative NC = 1M - 0.5M = 500k
    // NC = 500k - 100k = 400k
    assert(res.net_capital_usd == 400'000LL);

    // Min capital for first-year: max(50k, 3.5M * 0.125 = 437,500) = 437,500
    assert(res.minimum_net_capital_required_usd == 437'500ULL);
    assert(res.net_capital_usd < static_cast<int64_t>(res.minimum_net_capital_required_usd));
    assert(res.is_net_capital_compliant == false);
    assert(res.is_rule_17a11_critical_telegraphic_notice == true);
    assert(res.is_rule_17a11_early_warning == true);
    assert(res.is_audit_filing_approved == false);
}

void test_alternative_standard_debit_items() {
    Rule17a5FinancialSchedule sched{};
    sched.total_assets_usd = 20'000'000ULL;
    sched.total_liabilities_usd = 15'000'000ULL;
    sched.allowable_subordinated_debt_usd = 0;
    sched.non_allowable_assets_usd = 1'000'000ULL;
    sched.aggregate_debit_items_usd = 50'000'000ULL; // 2% of 50M = 1,000,000
    sched.total_haircuts_usd = 500'000ULL;
    sched.undue_concentration_charges_usd = 0;
    sched.use_alternative_standard = true;

    Rule17a5AuditVerification audit{};
    audit.pcaob_firm_registration_id = 777;
    audit.audit_opinion = AuditOpinionType::Unqualified;

    auto res = SecRule17a5AuditEngine::evaluate_filing(sched, audit, BrokerDealerClassification::CarryingClearingBroker);

    // Net worth = 5M, Tentative NC = 4M, NC = 3.5M
    assert(res.net_capital_usd == 3'500'000LL);
    // Min required: max(250k, 2% of 50M = 1M) = 1,000,000
    assert(res.minimum_net_capital_required_usd == 1'000'000ULL);
    assert(res.excess_net_capital_usd == 2'500'000LL);
    assert(res.is_net_capital_compliant == true);
    assert(res.is_ai_ratio_compliant == true);
    assert(res.is_rule_17a11_early_warning == false);
    assert(res.is_audit_filing_approved == true);
}

int main() {
    test_carrying_bd_compliant_ai_standard();
    test_first_year_and_early_warning_triggers();
    test_alternative_standard_debit_items();
    std::cout << "SEC Rule 17a-5 Broker-Dealer Audit Engine tests passed.\n";
    return 0;
}
