#include "luv_auction.hpp"
#include <cassert>
#include <cstdio>

void test_auction_clearing_price() {
    luv::auction::AuctionCrossEngine engine;
    uint16_t sym = 1;

    // Book state:
    // Buys:
    // Order 1: 500 @ 10050
    // Order 2: 300 @ 10040
    // Order 3: 200 @ 10030
    engine.add_auction_order(1, sym, luv::exec::kBuy, 10050, 500, false);
    engine.add_auction_order(2, sym, luv::exec::kBuy, 10040, 300, false);
    engine.add_auction_order(3, sym, luv::exec::kBuy, 10030, 200, false);

    // Sells:
    // Order 4: 400 @ 10030
    // Order 5: 350 @ 10040
    // Order 6: 500 @ 10060
    engine.add_auction_order(4, sym, luv::exec::kSell, 10030, 400, false);
    engine.add_auction_order(5, sym, luv::exec::kSell, 10040, 350, false);
    engine.add_auction_order(6, sym, luv::exec::kSell, 10060, 500, false);

    // At 10040:
    // Buy volume = 500 (from 10050) + 300 (from 10040) = 800
    // Sell volume = 400 (from 10030) + 350 (from 10040) = 750
    // Paired volume = min(800, 750) = 750
    // Imbalance = 800 - 750 = 50 (Buy Imbalance)

    auto noii = engine.compute_noii(sym, 10000);
    assert(noii.indicative_match_price == 10040);
    assert(noii.paired_qty == 750);
    assert(noii.imbalance_qty == 50);
    assert(noii.imbalance_side == luv::auction::ImbalanceSide::kBuyImbalance);

    std::printf("[PASS] test_auction_clearing_price (IMP: %lld, Paired: %lld, Imbalance: %lld)\n",
        static_cast<long long>(noii.indicative_match_price),
        static_cast<long long>(noii.paired_qty),
        static_cast<long long>(noii.imbalance_qty));
}

void test_market_on_open_orders() {
    luv::auction::AuctionCrossEngine engine;
    uint16_t sym = 2;

    // MOO (Market on Open) Buy 1000 shares
    engine.add_auction_order(10, sym, luv::exec::kBuy, 0, 1000, true);
    // Limit Sell 1000 @ 50000
    engine.add_auction_order(11, sym, luv::exec::kSell, 50000, 1000, false);

    auto noii = engine.compute_noii(sym, 49900);
    assert(noii.indicative_match_price == 50000);
    assert(noii.paired_qty == 1000);
    assert(noii.imbalance_qty == 0);
    assert(noii.imbalance_side == luv::auction::ImbalanceSide::kNone);

    std::printf("[PASS] test_market_on_open_orders\n");
}

int main() {
    test_auction_clearing_price();
    test_market_on_open_orders();
    std::printf("All auction crossing and NOII tests passed successfully.\n");
    return 0;
}
