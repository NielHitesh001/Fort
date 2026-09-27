#pragma once
#include "luv_historical_backtest.hpp"
#include "luv_lob.hpp"
namespace luv::historical {
// Call on the LOB owner thread after processing a feed update. This is a
// counterfactual snapshot: executions never mutate the historical source book.
// Fort ITCH prices use 1/10000 units; replay uses paise (1/100 INR).
inline Event from_lob(const LOBEngine& lob, const Arena& arena,
                      uint16_t symbol, uint64_t timestamp, int64_t divisor = 100) {
    if (symbol >= kSymbols || divisor <= 0) throw std::invalid_argument("invalid LOB adapter parameters");
    Event e{}; e.timestamp = timestamp; e.symbol = symbol;
    for (size_t i = 0; i < std::min<size_t>(kDepth, lob.bid_level_count(symbol)); ++i) {
        const auto& l = arena.level(symbol, 0, i);
        if (l.price % divisor) throw std::invalid_argument("LOB price cannot be represented in paise");
        e.book.bids[i] = {l.price / divisor, l.total_qty};
    }
    for (size_t i = 0; i < std::min<size_t>(kDepth, lob.ask_level_count(symbol)); ++i) {
        const auto& l = arena.level(symbol, 1, i);
        if (l.price % divisor) throw std::invalid_argument("LOB price cannot be represented in paise");
        e.book.asks[i] = {l.price / divisor, l.total_qty};
    }
    return e;
}
}
