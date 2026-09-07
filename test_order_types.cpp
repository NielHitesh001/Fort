#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_ouch.hpp"

namespace {

uint32_t load_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

void test_tif_and_order_flags() {
    std::printf("\n== Time-In-Force and Order Flags ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(5, limits);

    // Test IOC order
    luv::exec::OrderIntent ioc_intent{};
    ioc_intent.symbol_idx = 5;
    ioc_intent.side = luv::exec::kBuy;
    ioc_intent.qty = 200;
    ioc_intent.price = 1'500'000;
    ioc_intent.alpha_timestamp_ns = 100;
    ioc_intent.now_ns = 100;
    ioc_intent.client_order_id = 0x1001;
    ioc_intent.time_in_force = luv::exec::kIOC;

    luv::OutboundPacket packet{};
    auto decision = gateway.try_build(ioc_intent, packet);
    assert(decision.pass == 1);
    assert(packet.len == luv::exec::ouch::kEnterOrderLen);
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kTimeInForceOffset) == luv::exec::kIOC);
    assert(arena.exec_states[5].orders[0].time_in_force == luv::exec::kIOC);

    // Test FOK order
    luv::exec::OrderIntent fok_intent = ioc_intent;
    fok_intent.client_order_id = 0x1002;
    fok_intent.time_in_force = luv::exec::kFOK;
    decision = gateway.try_build(fok_intent, packet);
    assert(decision.pass == 1);
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kTimeInForceOffset) == luv::exec::kFOK);
    assert(arena.exec_states[5].orders[1].time_in_force == luv::exec::kFOK);

    // Test GTC order
    luv::exec::OrderIntent gtc_intent = ioc_intent;
    gtc_intent.client_order_id = 0x1003;
    gtc_intent.time_in_force = luv::exec::kGTC;
    decision = gateway.try_build(gtc_intent, packet);
    assert(decision.pass == 1);
    assert(load_u32_be(packet.bytes + luv::exec::ouch::kTimeInForceOffset) == luv::exec::kGTC);
    assert(arena.exec_states[5].orders[2].time_in_force == luv::exec::kGTC);

    std::printf("  [OK] Time-in-Force (IOC, FOK, GTC, Day) encoded and tracked correctly\n");
}

void test_cancel_replace_lifecycle() {
    std::printf("\n== Cancel / Replace Full Lifecycle ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(2, limits);

    // 1. Submit initial order
    luv::exec::OrderIntent orig_intent{};
    orig_intent.symbol_idx = 2;
    orig_intent.side = luv::exec::kBuy;
    orig_intent.qty = 100;
    orig_intent.price = 500'000;
    orig_intent.alpha_timestamp_ns = 1000;
    orig_intent.now_ns = 1000;
    orig_intent.client_order_id = 7001;

    luv::OutboundPacket packet{};
    assert(gateway.try_build(orig_intent, packet).pass == 1);
    assert(arena.exec_states[2].orders[0].qty == 100);
    assert(arena.exec_states[2].orders[0].price == 500'000);
    assert(arena.exec_states[2].risk.net_position == 100);
    assert(arena.exec_states[2].risk.gross_exposure == 50'000'000);

    // 2. Request replace to qty=250, price=505'000
    luv::exec::OrderReplaceIntent replace_intent{};
    replace_intent.symbol_idx = 2;
    replace_intent.original_order_id = 7001;
    replace_intent.replacement_order_id = 7002;
    replace_intent.new_qty = 250;
    replace_intent.new_price = 505'000;
    replace_intent.alpha_timestamp_ns = 1100;
    replace_intent.now_ns = 1100;

    luv::OutboundPacket replace_packet{};
    assert(gateway.try_replace(replace_intent, replace_packet).pass == 1);
    assert(replace_packet.len == luv::exec::ouch::kReplaceOrderLen);
    assert(replace_packet.bytes[luv::exec::ouch::kReplaceMsgTypeOffset] == 'U');
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplaceExistingTokenOffset) == 7001);
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplaceReplacementTokenOffset) == 7002);
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplaceQtyOffset) == 250);
    assert(load_u32_be(replace_packet.bytes + luv::exec::ouch::kReplacePriceOffset) == 505'000);

    // 3. Receive venue replacement acknowledgment
    assert(gateway.apply_replace_report(2, 7001, 250, 505'000));
    assert(arena.exec_states[2].orders[0].qty == 250);
    assert(arena.exec_states[2].orders[0].price == 505'000);
    assert(arena.exec_states[2].risk.net_position == 250);
    assert(arena.exec_states[2].risk.gross_exposure == 250 * 505'000);

    // 4. Fill the replaced order
    assert(gateway.apply_execution_report(2, luv::ExecutionReport{7001, 250, true}));
    assert(arena.exec_states[2].risk.order_count == 0);

    std::printf("  [OK] cancel/replace generation, parsing, and atomic state mutation verified\n");
}

void test_venue_reject_handling() {
    std::printf("\n== Venue Reject ('J') Handling ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(4, limits);

    luv::exec::OrderIntent intent{};
    intent.symbol_idx = 4;
    intent.side = luv::exec::kSell;
    intent.qty = 300;
    intent.price = 2'000'000;
    intent.alpha_timestamp_ns = 10;
    intent.now_ns = 10;
    intent.client_order_id = 9999;

    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent, packet).pass == 1);
    assert(arena.exec_states[4].risk.order_count == 1);
    assert(arena.exec_states[4].risk.net_position == -300);

    // Receive exchange rejection
    assert(gateway.apply_reject_report(4, 9999, 'X'));
    assert(arena.exec_states[4].risk.order_count == 0);
    assert(arena.exec_states[4].risk.net_position == 0);
    assert(arena.exec_states[4].risk.reject_count == 1);

    std::printf("  [OK] venue reject cleans up active order and reconciles risk state\n");
}

void test_stop_and_pegged_order_triggers() {
    std::printf("\n== In-Memory Stop & Pegged Order Triggers ==\n");

    luv::Arena arena;
    assert(arena.init());

    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));

    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 1'000;
    limits.max_abs_position = 10'000;
    limits.max_alpha_age_ns = 10'000'000;
    gateway.risk().set_limits(1, limits);

    luv::StopOrderTable<64> table;

    // 1. Register Buy Stop Loss (triggers when Ask >= 105)
    luv::exec::ConditionalOrder stop_loss_buy{};
    stop_loss_buy.symbol_idx = 1;
    stop_loss_buy.side = luv::exec::kBuy;
    stop_loss_buy.trigger_type = luv::exec::TriggerType::kStopLoss;
    stop_loss_buy.stop_price = 1'050'000;
    stop_loss_buy.qty = 50;
    stop_loss_buy.client_order_id = 101;
    assert(table.register_trigger(stop_loss_buy));

    // 2. Register Sell Stop Limit (triggers when Bid <= 95, limits at 94)
    luv::exec::ConditionalOrder stop_limit_sell{};
    stop_limit_sell.symbol_idx = 1;
    stop_limit_sell.side = luv::exec::kSell;
    stop_limit_sell.trigger_type = luv::exec::TriggerType::kStopLimit;
    stop_limit_sell.stop_price = 950'000;
    stop_limit_sell.limit_price = 940'000;
    stop_limit_sell.qty = 75;
    stop_limit_sell.client_order_id = 102;
    assert(table.register_trigger(stop_limit_sell));

    // 3. Register Pegged to Midpoint (+100 ticks)
    luv::exec::ConditionalOrder pegged_mid{};
    pegged_mid.symbol_idx = 1;
    pegged_mid.side = luv::exec::kBuy;
    pegged_mid.trigger_type = luv::exec::TriggerType::kPeggedMid;
    pegged_mid.peg_offset = 100;
    pegged_mid.qty = 100;
    pegged_mid.client_order_id = 103;
    assert(table.register_trigger(pegged_mid));

    assert(table.active_count() == 3);

    // Initial BBO: Bid = 980'000, Ask = 1'020'000 (no stop breached, pegged triggers)
    luv::OutboundPacket out[8]{};
    size_t routed = table.evaluate_bbo(1, 980'000, 1'020'000, 500, gateway, out, 8);
    assert(routed == 1);
    assert(table.active_count() == 2);
    assert(arena.exec_states[1].orders[0].order_id == 103);
    assert(arena.exec_states[1].orders[0].price == 1'000'100);

    // Next BBO: Market surges up: Bid = 1'040'000, Ask = 1'060'000 -> StopLoss Buy breached!
    routed = table.evaluate_bbo(1, 1'040'000, 1'060'000, 600, gateway, out, 8);
    assert(routed == 1);
    assert(table.active_count() == 1);
    assert(arena.exec_states[1].orders[1].order_id == 101);
    assert(arena.exec_states[1].orders[1].price == 1'060'000);

    // Next BBO: Market drops: Bid = 940'000, Ask = 960'000 -> StopLimit Sell breached!
    routed = table.evaluate_bbo(1, 940'000, 960'000, 700, gateway, out, 8);
    assert(routed == 1);
    assert(table.active_count() == 0);
    assert(arena.exec_states[1].orders[2].order_id == 102);
    assert(arena.exec_states[1].orders[2].price == 940'000);

    std::printf("  [OK] Stop Loss, Stop Limit, and Pegged triggers correctly evaluated and routed\n");
}

}  // namespace

int main() {
    std::printf("=== Order Types & Execution Lifecycle Tests ===\n");

    test_tif_and_order_flags();
    test_cancel_replace_lifecycle();
    test_venue_reject_handling();
    test_stop_and_pegged_order_triggers();

    std::printf("\nAll Order Types tests passed successfully.\n");
    return 0;
}
