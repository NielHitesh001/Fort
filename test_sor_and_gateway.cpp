#include <cassert>
#include <cstdio>
#include <cstring>
#include "luv_arena.hpp"
#include "luv_gateway.hpp"
#include "luv_health.hpp"
#include "luv_lob.hpp"
#include "luv_safety.hpp"
#include "luv_sor.hpp"

using namespace luv;

void test_smart_order_router() {
    std::printf("[test_smart_order_router] Running...\n");
    SmartOrderRouter sor;

    // Set venue quotes:
    // Nasdaq: Ask $100, Qty 500, Fee 10 bps
    // Bats:   Ask $99,  Qty 300, Fee 5 bps (Better price & fee!)
    // Nyse:   Ask $100, Qty 400, Fee 8 bps
    sor.update_venue_quote(VenueId::kNasdaq, 98 * 10'000, 1000, 100 * 10'000, 500, 10);
    sor.update_venue_quote(VenueId::kBats,   98 * 10'000, 1000, 99 * 10'000,  300, 5);
    sor.update_venue_quote(VenueId::kNyse,   98 * 10'000, 1000, 100 * 10'000, 400, 8);

    // Route a buy order of 600 shares with limit price $100
    auto slices = sor.route_order(exec::kBuy, 600, 100 * 10'000, 5000);

    // Should route first slice to Bats (300 shares @ $99)
    assert(slices.size() == 2);
    assert(slices[0].venue == VenueId::kBats);
    assert(slices[0].qty == 300);
    assert(slices[0].price == 99 * 10'000);

    // Second slice to Nyse (300 shares @ $100, lower fee 8 bps vs Nasdaq 10 bps)
    assert(slices[1].venue == VenueId::kNyse);
    assert(slices[1].qty == 300);
    assert(slices[1].price == 100 * 10'000);

    std::printf("[test_smart_order_router] PASSED\n");
}

void test_health_monitor() {
    std::printf("[test_health_monitor] Running...\n");
    Arena arena;
    assert(arena.init());

    CircuitBreaker cb(3);

    // Normal state -> Healthy
    assert(SystemHealthMonitor::evaluate(arena, cb) == HealthProbeStatus::kHealthy);

    char health_json[512]{};
    assert(SystemHealthMonitor::format_healthz_json(HealthProbeStatus::kHealthy, arena, cb, health_json, sizeof(health_json)));
    assert(std::strstr(health_json, "\"status\": \"HEALTHY\"") != nullptr);

    // Tripped circuit breaker -> Unhealthy
    cb.trip(1000);
    assert(SystemHealthMonitor::evaluate(arena, cb) == HealthProbeStatus::kUnhealthy);

    std::printf("[test_health_monitor] PASSED\n");
}

void test_gateway_parser() {
    std::printf("[test_gateway_parser] Running...\n");

    const char* req_json = "{\"action\":\"NEW\",\"client_order_id\":1050,\"symbol_idx\":2,\"side\":\"BUY\",\"price\":1500000,\"qty\":200}";
    GatewayOrderRequest req{};
    assert(OrderGatewayParser::parse_json_request(req_json, req));
    assert(req.type == GatewayMsgType::kNewOrderSingle);
    assert(req.client_order_id == 1050);
    assert(req.symbol_idx == 2);
    assert(req.side == exec::kBuy);
    assert(req.price == 1500000);
    assert(req.qty == 200);

    Arena arena;
    assert(arena.init());
    LOBEngine lob;
    assert(lob.init(arena));

    char l2_json[512]{};
    assert(OrderGatewayParser::format_l2_snapshot_json(lob, 0, 5, l2_json, sizeof(l2_json)));
    assert(std::strstr(l2_json, "\"event\": \"L2_SNAPSHOT\"") != nullptr);

    std::printf("[test_gateway_parser] PASSED\n");
}

int main() {
    test_smart_order_router();
    test_health_monitor();
    test_gateway_parser();
    std::printf("ALL SOR, HEALTH & GATEWAY TESTS PASSED\n");
    return 0;
}
