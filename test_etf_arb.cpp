#include "luv_etf_arb.hpp"
#include <cassert>
#include <cstdio>

void test_etf_creation_redemption_arbitrage() {
    luv::arb::EtfArbitrageEngine engine;

    // Basket for 50,000 ETF creation unit:
    // Stock A: 10,000 shares @ $100.00 (1000000) -> $1,000,000 value
    // Stock B: 20,000 shares @ $200.00 (2000000) -> $4,000,000 value
    // Total Basket = $5,000,000 -> iNAV per share = $5,000,000 / 50,000 = $100.00 (1000000)
    assert(engine.add_constituent(1, 10'000, 100'0000LL));
    assert(engine.add_constituent(2, 20'000, 200'0000LL));

    int64_t inav = engine.compute_inav();
    assert(inav == 100'0000LL); // $100.0000

    // Scenario 1: ETF Market trading at strong premium: Bid = $100.50 (1005000)
    // Arbitrage Signal: Create ETF (Buy basket, create ETF, sell at $100.50)
    auto opp1 = engine.evaluate_arbitrage(100'5000LL, 100'5500LL);
    assert(opp1.signal == luv::arb::ArbSignalType::kCreateEtfArb);
    assert(opp1.net_mispricing_bps >= 40);

    // Scenario 2: ETF Market trading at strong discount: Ask = $99.40 (994000)
    // Arbitrage Signal: Redeem ETF (Buy ETF at $99.40, redeem basket, sell stocks)
    auto opp2 = engine.evaluate_arbitrage(99'3500LL, 99'4000LL);
    assert(opp2.signal == luv::arb::ArbSignalType::kRedeemEtfArb);
    assert(opp2.net_mispricing_bps >= 50);

    // Scenario 3: ETF trading tight around $100.00 -> Neutral
    auto opp3 = engine.evaluate_arbitrage(99'9900LL, 100'0100LL);
    assert(opp3.signal == luv::arb::ArbSignalType::kNeutral);

    std::printf("[PASS] test_etf_creation_redemption_arbitrage (iNAV: $%lld.%04lld, Creation Arb: %lld bps, Redemption Arb: %lld bps)\n",
        static_cast<long long>(inav / 10000), static_cast<long long>(inav % 10000),
        static_cast<long long>(opp1.net_mispricing_bps),
        static_cast<long long>(opp2.net_mispricing_bps));
}

int main() {
    test_etf_creation_redemption_arbitrage();
    std::printf("All ETF creation/redemption arbitrage tests passed successfully.\n");
    return 0;
}
