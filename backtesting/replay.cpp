#include "luv_historical_backtest.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
using namespace luv::historical;
namespace {
int64_t integer(const std::string& s) {
    int64_t value = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || p != s.data() + s.size()) throw std::runtime_error("invalid integer: " + s);
    return value;
}
double number(const std::string& s) {
    size_t n = 0; const double value = std::stod(s, &n);
    if (n != s.size() || !std::isfinite(value)) throw std::runtime_error("invalid number");
    return value;
}
std::vector<Event> read(const char* path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open events");
    std::string line; std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string expected = "timestamp,symbol,kind,open,high,low,close,volume";
    for (int i = 0; i < 5; ++i) expected += ",bid" + std::to_string(i) + ",bid_qty" + std::to_string(i) + ",ask" + std::to_string(i) + ",ask_qty" + std::to_string(i);
    if (line != expected) throw std::runtime_error("invalid normalized event header");
    std::vector<Event> events;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream row(line); std::string cell;
        std::vector<int64_t> v;
        while (std::getline(row, cell, ',')) v.push_back(integer(cell));
        if (v.size() != 28 || v[0] <= 0 || v[1] < 0 || v[1] >= static_cast<int64_t>(kSymbols) || v[2] < 0 || v[2] > 2)
            throw std::runtime_error("invalid event row");
        Event e{}; e.timestamp = static_cast<uint64_t>(v[0]); e.symbol = static_cast<uint16_t>(v[1]); e.kind = static_cast<Kind>(v[2]);
        e.open = v[3]; e.high = v[4]; e.low = v[5]; e.close = v[6]; e.volume = v[7];
        for (size_t i = 0; i < kDepth; ++i) { e.book.bids[i] = {v[8+i*4], v[9+i*4]}; e.book.asks[i] = {v[10+i*4], v[11+i*4]}; }
        if (!Engine::valid(e) || (!events.empty() && e.timestamp < events.back().timestamp)) throw std::runtime_error("invalid or unsorted market data");
        events.push_back(e);
    }
    if (events.empty()) throw std::runtime_error("empty market data");
    return events;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 13) throw std::runtime_error("usage: fort_replay EVENTS OUT CASH FEE_BPS SLIP_BPS MAX_POS LATENCY_NS LOOKBACK THRESHOLD QTY mean_reversion|momentum FILL_CAPACITY");
        const auto events = read(argv[1]);
        const auto latency = integer(argv[7]), capacity = integer(argv[12]), lookback = integer(argv[8]);
        if (latency < 0 || capacity < 1 || capacity > 10'000'000 || lookback < 2 || lookback > 256) throw std::runtime_error("invalid capacity, latency or lookback");
        const std::string strategy_name = argv[11];
        if (strategy_name != "mean_reversion" && strategy_name != "momentum") throw std::runtime_error("unknown strategy");
        Settings settings{number(argv[3]), number(argv[4]), number(argv[5]), integer(argv[6]), static_cast<uint64_t>(latency)};
        Engine engine(settings, events.size(), static_cast<size_t>(capacity));
        RollingStrategy strategy(static_cast<size_t>(lookback), number(argv[9]), integer(argv[10]), strategy_name == "momentum");
        for (const auto& e : events) if (!engine.step(e, strategy)) throw std::runtime_error(engine.error());
        const std::filesystem::path dir(argv[2]);
        std::filesystem::create_directories(dir);
        std::ofstream fills(dir / "trades.csv"), equity(dir / "equity.csv"), positions(dir / "positions.csv");
        fills.exceptions(std::ios::badbit | std::ios::failbit); equity.exceptions(std::ios::badbit | std::ios::failbit); positions.exceptions(std::ios::badbit | std::ios::failbit);
        fills << std::setprecision(17) << "timestamp,order_id,symbol,side,qty,price,commission,slippage,realized,closed_qty\n";
        for (size_t i = 0; i < engine.fill_count(); ++i) {
            const auto& f = engine.fill(i);
            fills << f.timestamp << ',' << f.order_id << ',' << f.symbol << ',' << (f.side == Side::Buy ? "buy" : "sell") << ',' << f.qty << ',' << f.price / 100.0 << ',' << f.commission << ',' << f.slippage << ',' << f.realized << ',' << f.closed_qty << '\n';
        }
        equity << std::setprecision(17) << "timestamp,cash,equity,commission,slippage\n";
        for (size_t i = 0; i < engine.sample_count(); ++i) {
            const auto& s = engine.sample(i);
            equity << s.timestamp << ',' << s.cash << ',' << s.equity << ',' << s.commission << ',' << s.slippage << '\n';
        }
        positions << std::setprecision(17) << "symbol,quantity,cost_basis,realized_pnl,pending\n";
        for (uint16_t i = 0; i < kSymbols; ++i) {
            const auto& p = engine.position(i);
            if (p.qty || p.realized || engine.pending(i)) positions << i << ',' << p.qty << ',' << p.cost << ',' << p.realized << ',' << engine.pending(i) << '\n';
        }
        fills.close(); equity.close(); positions.close();
        std::cout << "{\"events\":" << events.size() << ",\"fills\":" << engine.fill_count() << ",\"rejections\":" << engine.rejections() << "}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
