#include "luv_sor_sim.hpp"
#include <cassert>
#include <cstdio>

void test_smart_order_routing_multi_venue() {
    luv::routing::SmartOrderRoutingSimulator sor;

    // Venue 1 (NYSE): Ask $100.02 (1000200) for 500 shares, Latency 400ns
    assert(sor.update_venue(luv::routing::VenueQuote{
        .venue_id = 1,
        .bid_price = 999800,
        .ask_price = 1000200,
        .bid_qty = 500,
        .ask_qty = 500,
        .latency_to_venue_ns = 400
    }));

    // Venue 2 (Nasdaq): Ask $100.01 (1000100) for 300 shares, Latency 200ns (Cheapest price!)
    assert(sor.update_venue(luv::routing::VenueQuote{
        .venue_id = 2,
        .bid_price = 999900,
        .ask_price = 1000100,
        .bid_qty = 300,
        .ask_qty = 300,
        .latency_to_venue_ns = 200
    }));

    // Venue 3 (BATS): Ask $100.03 (1000300) for 1000 shares, Latency 300ns
    assert(sor.update_venue(luv::routing::VenueQuote{
        .venue_id = 3,
        .bid_price = 999700,
        .ask_price = 1000300,
        .bid_qty = 1000,
        .ask_qty = 1000,
        .latency_to_venue_ns = 300
    }));

    // Route a Buy of 600 shares:
    // Should take 300 shares from Venue 2 @ 100.01 (best price)
    // Then 300 shares from Venue 1 @ 100.02 (next best price)
    std::array<luv::routing::RoutedSliceResult, 4> slices{};
    size_t count = sor.route_order(luv::exec::kBuy, 600, slices.data(), slices.size());

    assert(count == 2);
    assert(slices[0].venue_id == 2 && slices[0].executed_qty == 300 && slices[0].executed_price == 1000100);
    assert(slices[1].venue_id == 1 && slices[1].executed_qty == 300 && slices[1].executed_price == 1000200);

    std::printf("[PASS] test_smart_order_routing_multi_venue (Routed 600 shares across %zu venues)\n", count);
}

int main() {
    test_smart_order_routing_multi_venue();
    std::printf("All SOR multi-venue simulation tests passed successfully.\n");
    return 0;
}
