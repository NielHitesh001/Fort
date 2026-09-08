#include "luv_pre_trade_risk_security.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_normal_order_and_notional_limits() {
    PreTradeRiskConfig cfg{};
    cfg.max_single_order_notional_usd = 100'000ULL; // $100k
    cfg.max_cumulative_notional_usd = 500'000ULL;   // $500k
    cfg.max_price_collar_fraction = 0.05;          // 5%

    PreTradeRiskSecurityFirewall firewall(cfg);
    uint64_t now_ns = 1'000'000'000ULL;

    // Normal order: 100 shares @ $150.00 = $15,000 USD (BBO: $149.95 - $150.05)
    auto res1 = firewall.validate_order(1, true, 15000, 100, 14995, 15005, now_ns);
    assert(res1 == RiskCheckResult::Approved);
    assert(firewall.current_gross_notional() == 15'000ULL);

    // Single order exceeds max notional: 1000 shares @ $150.00 = $150,000 USD > $100,000 limit
    auto res2 = firewall.validate_order(2, true, 15000, 1000, 14995, 15005, now_ns);
    assert(res2 == RiskCheckResult::ExceedsMaxNotional);

    // Fat finger collar breach: Buy @ $170.00 when BBO is $150.00 (+13.3% > 5%)
    auto res3 = firewall.validate_order(3, true, 17000, 100, 14995, 15005, now_ns);
    assert(res3 == RiskCheckResult::FatFingerPriceCollarBreach);
}

void test_kill_switch_and_credit_exhaustion() {
    PreTradeRiskConfig cfg{};
    cfg.max_single_order_notional_usd = 100'000ULL;
    cfg.max_cumulative_notional_usd = 30'000ULL; // Only $30k total credit

    PreTradeRiskSecurityFirewall firewall(cfg);
    uint64_t now_ns = 2'000'000'000ULL;

    // Order 1: $20,000
    assert(firewall.validate_order(1, true, 20000, 100, 19995, 20005, now_ns) == RiskCheckResult::Approved);

    // Order 2: $20,000 -> Exceeds cumulative $30,000 limit
    assert(firewall.validate_order(2, true, 20000, 100, 19995, 20005, now_ns) == RiskCheckResult::CreditLimitExceeded);

    // Trigger emergency kill switch
    firewall.trigger_emergency_kill_switch();
    assert(firewall.is_kill_switch_active() == true);

    // Even small order rejected when kill switch active
    assert(firewall.validate_order(3, true, 1000, 1, 995, 1005, now_ns) == RiskCheckResult::KillSwitchActive);
}

int main() {
    test_normal_order_and_notional_limits();
    test_kill_switch_and_credit_exhaustion();
    std::cout << "Pre-Trade Risk Security Firewall tests passed.\n";
    return 0;
}
