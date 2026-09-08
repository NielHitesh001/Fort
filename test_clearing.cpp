#include "luv_clearing.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

void test_continuous_net_settlement() {
    luv::clearing::ClearingHouseEngine clearing;
    uint32_t broker_a = 101;
    uint32_t broker_b = 102;
    uint16_t sym = 1;

    // Trade 1: Broker A buys 500 @ $100.00 ($50k) from Broker B
    luv::clearing::TradeRecord t1{
        .trade_id = 1,
        .buy_broker_id = broker_a,
        .sell_broker_id = broker_b,
        .symbol_idx = sym,
        .price = 1000000, // $100.00 x 10^4
        .qty = 500,
        .timestamp_ns = 1000
    };
    assert(clearing.record_and_net_trade(t1));

    // Trade 2: Broker B buys 200 @ $101.00 ($20.2k) from Broker A
    luv::clearing::TradeRecord t2{
        .trade_id = 2,
        .buy_broker_id = broker_b,
        .sell_broker_id = broker_a,
        .symbol_idx = sym,
        .price = 1010000,
        .qty = 200,
        .timestamp_ns = 2000
    };
    assert(clearing.record_and_net_trade(t2));

    // Verify netted position for Broker A:
    // Net Qty = +500 - 200 = +300 (Long 300 shares)
    // Net Cash = -$50,000 + $20,200 = -$29,800 (Payable $29.8k)
    auto* pos_a = clearing.get_broker_position(broker_a, sym);
    assert(pos_a != nullptr);
    assert(pos_a->net_qty == 300);
    assert(pos_a->net_cash == -29800);
    assert(pos_a->gross_trades == 2);

    // Verify netted position for Broker B:
    // Net Qty = -500 + 200 = -300 (Short 300 shares)
    // Net Cash = +$50,000 - $20,200 = +$29,800 (Receivable $29.8k)
    auto* pos_b = clearing.get_broker_position(broker_b, sym);
    assert(pos_b != nullptr);
    assert(pos_b->net_qty == -300);
    assert(pos_b->net_cash == 29800);
    assert(pos_b->gross_trades == 2);

    std::printf("[PASS] test_continuous_net_settlement\n");
}

void test_iso20022_setr016_formatting() {
    char xml[1024];
    size_t len = luv::clearing::ClearingHouseEngine::format_iso20022_setr016(
        xml, sizeof(xml),
        998877,
        "GSCOUS33",
        "MSCOUS33",
        "US0378331005",
        1502500,
        500,
        1725753600000000000ULL
    );

    assert(len > 0);
    assert(std::strstr(xml, "<TxId>998877</TxId>"));
    assert(std::strstr(xml, "<ISIN>US0378331005</ISIN>"));
    assert(std::strstr(xml, "<BIC>GSCOUS33</BIC>"));
    assert(std::strstr(xml, "<BIC>MSCOUS33</BIC>"));
    assert(std::strstr(xml, "<Unit>500</Unit>"));

    std::printf("[PASS] test_iso20022_setr016_formatting\n");
}

int main() {
    test_continuous_net_settlement();
    test_iso20022_setr016_formatting();
    std::printf("All post-trade clearing & ISO 20022 tests passed successfully.\n");
    return 0;
}
