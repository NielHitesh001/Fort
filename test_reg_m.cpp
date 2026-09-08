#include "luv_reg_m.hpp"
#include <cassert>
#include <cstdio>

void test_sec_reg_m_offering_restrictions() {
    luv::compliance::RegMValidator validator;

    // Offering for Symbol 1 (IPO/Follow-on offering):
    // Syndicate MPID: 101
    // Restricted period: T=1000 to T=5000 (pricing date)
    assert(validator.register_offering(luv::compliance::RegMDistributionOffering{
        .symbol_idx = 1,
        .participant_mpid = 101,
        .restricted_period_start_ns = 1000,
        .pricing_date_ns = 5000,
        .offering_price = 50'0000LL,
        .is_actively_traded_exempt = false
    }));

    // 1. Inside restricted period (T=3000): Syndicate MPID 101 tries to Bid (Buy) -> Prohibited
    auto r1 = validator.validate_order(1, 101, luv::exec::kBuy, 50'0000LL, 3000);
    assert(r1 == luv::compliance::RegMRestrictedReason::kRestrictedPeriodBiddingProhibited);

    // 2. Inside restricted period (T=3000): Syndicate MPID 101 submits Sell order -> Allowed
    auto r2 = validator.validate_order(1, 101, luv::exec::kSell, 50'0000LL, 3000);
    assert(r2 == luv::compliance::RegMRestrictedReason::kNone);

    // 3. Inside restricted period (T=3000): Unaffiliated MPID 202 tries to Buy -> Allowed
    auto r3 = validator.validate_order(1, 202, luv::exec::kBuy, 50'0000LL, 3000);
    assert(r3 == luv::compliance::RegMRestrictedReason::kNone);

    // 4. After pricing (T=6000): Syndicate MPID 101 buys -> Allowed
    auto r4 = validator.validate_order(1, 101, luv::exec::kBuy, 50'0000LL, 6000);
    assert(r4 == luv::compliance::RegMRestrictedReason::kNone);

    std::printf("[PASS] test_sec_reg_m_offering_restrictions\n");
}

int main() {
    test_sec_reg_m_offering_restrictions();
    std::printf("All SEC Regulation M tests passed successfully.\n");
    return 0;
}
