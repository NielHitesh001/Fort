#include <cassert>
#include <cstdint>
#include <cstring>

#include "luv_decode_itch.hpp"
#include "luv_execution.hpp"
#include "luv_logging.hpp"

namespace {
luv::LogEvent last_event{};
uint32_t event_count = 0;

void capture(const luv::LogEvent& event) noexcept {
    last_event = event;
    ++event_count;
}
}  // namespace

int main() {
    luv::StructuredLogger::set_sink(&capture);
    luv::SymbolTable symbols;
    luv::TickMsg out{};
    const uint8_t short_message[] = {'A'};
    assert(!luv::decode_itch(short_message, sizeof(short_message), symbols, out));
    assert(event_count == 1);
    assert(std::strcmp(last_event.module, "decode") == 0);
    assert(std::strcmp(last_event.event, "message_too_short") == 0);
    assert(last_event.value1 == 1 && last_event.value2 == 11);

    luv::Arena arena;
    assert(arena.init());
    luv::PreTradeRisk risk;
    assert(risk.init(arena));
    luv::exec::RiskLimits invalid{};
    invalid.max_order_qty = 0;
    assert(!risk.set_limits(0, invalid));
    assert(event_count == 2);
    assert(std::strcmp(last_event.module, "risk") == 0);
    assert(std::strcmp(last_event.event, "invalid_limits") == 0);

    luv::StructuredLogger::set_sink(nullptr);
    return 0;
}
