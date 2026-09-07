#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <unistd.h>

#include "luv_ouch.hpp"

namespace {
void put_u64_be(uint8_t* data, uint64_t value) {
    for (int index = 7; index >= 0; --index) {
        data[index] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

void put_u32_be(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value >> 24);
    data[1] = static_cast<uint8_t>(value >> 16);
    data[2] = static_cast<uint8_t>(value >> 8);
    data[3] = static_cast<uint8_t>(value);
}
}

int main() {
    luv::ouch::Parser parser;
    luv::ouch::Event event{};
    size_t consumed = 0;

    std::array<uint8_t, 15> executed{};
    executed[0] = 0;
    executed[1] = 13;
    executed[2] = 'E';
    put_u64_be(executed.data() + 3, 42);
    put_u32_be(executed.data() + 11, 25);
    assert(parser.parse(executed.data(), executed.size(), event, consumed));
    assert(event.type == luv::ouch::EventType::kExecuted);
    assert(event.order_id == 42 && event.quantity == 25);
    assert(consumed == executed.size());

    std::array<uint8_t, 19> replaced{};
    replaced[0] = 0;
    replaced[1] = 17;
    replaced[2] = 'U';
    put_u64_be(replaced.data() + 3, 42);
    put_u32_be(replaced.data() + 11, 50);
    put_u32_be(replaced.data() + 15, 1'050'000);
    assert(parser.parse(replaced.data(), replaced.size(), event, consumed));
    assert(event.type == luv::ouch::EventType::kReplaced);
    assert(event.order_id == 42 && event.quantity == 50 && event.price == 1'050'000);
    assert(consumed == replaced.size());

    luv::Arena arena;
    assert(arena.init());
    luv::ExecutionGateway gateway;
    assert(gateway.init(arena));
    luv::exec::RiskLimits limits{};
    limits.max_order_qty = 100;
    limits.max_abs_position = 1'000;
    limits.max_alpha_age_ns = 1'000'000;
    gateway.risk().set_limits(3, limits);
    luv::exec::OrderIntent intent{};
    intent.symbol_idx = 3;
    intent.qty = 25;
    intent.price = 1'000'000;
    intent.alpha_timestamp_ns = 10;
    intent.now_ns = 10;
    intent.client_order_id = 42;
    luv::OutboundPacket packet{};
    assert(gateway.try_build(intent, packet).pass == 1);

    // Test replace through OrderFlowAdapter
    assert(luv::ouch::OrderFlowAdapter{}.consume(
        replaced.data(), replaced.size(), 3, gateway, consumed));
    assert(arena.exec_states[3].orders[0].qty == 50);
    assert(arena.exec_states[3].orders[0].price == 1'050'000);

    std::array<uint8_t, 15> fill = executed;
    put_u32_be(fill.data() + 11, 50);
    assert(luv::ouch::OrderFlowAdapter{}.consume(
        fill.data(), fill.size(), 3, gateway, consumed));
    assert(arena.exec_states[3].risk.order_count == 0);

    std::mt19937_64 rng(0xBADC0DE);
    for (uint32_t iteration = 0; iteration < 100'000; ++iteration) {
        std::array<uint8_t, 64> input{};
        for (uint8_t& byte : input) byte = static_cast<uint8_t>(rng());
        const size_t length = static_cast<size_t>(rng() % input.size());
        (void)parser.parse(input.data(), length, event, consumed);
    }

    assert(!parser.parse(nullptr, 0, event, consumed));
    assert(consumed == 0);
    std::printf("OUCH parser fuzz smoke passed: 100000 inputs\n");
    return 0;
}
