#pragma once
// Historical snapshot execution, independent from the live gateway. Prices are
// integer paise; accounting is INR. All storage is acquired before replay.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace luv::historical {
constexpr size_t kSymbols = 64, kOrders = 256, kDepth = 5;
constexpr int64_t kMaxPrice = 1'000'000'000, kMaxQty = 1'000'000'000;
enum class Side : uint8_t { Buy, Sell };
enum class Kind : uint8_t { Open, Bar, Quote };
struct Level { int64_t price = 0, qty = 0; };
struct Book { std::array<Level, kDepth> bids{}, asks{}; };
struct Event {
    uint64_t timestamp = 0;
    uint16_t symbol = 0;
    Kind kind = Kind::Quote;
    Book book{};
    int64_t open = 0, high = 0, low = 0, close = 0, volume = 0;
};
struct Position { int64_t qty = 0; double cost = 0, realized = 0; };
struct Fill {
    uint64_t timestamp = 0, order_id = 0;
    uint16_t symbol = 0;
    Side side = Side::Buy;
    int64_t qty = 0, price = 0;
    double commission = 0, slippage = 0, realized = 0;
    int64_t closed_qty = 0;
};
struct Sample { uint64_t timestamp = 0; double cash = 0, equity = 0, commission = 0, slippage = 0; };
struct Settings {
    double initial_cash = 1'000'000, commission_bps = 3, slippage_bps = 2;
    int64_t max_position = 100'000;
    uint64_t latency_ns = 0;
};
class Engine;
class Strategy {
public:
    virtual ~Strategy() = default;
    // Callbacks must not allocate. Bar callbacks see only completed bars.
    virtual void on_market(const Event&, Engine&) noexcept = 0;
    virtual void on_fill(const Fill&, Engine&) noexcept {}
};
class Engine {
    struct Order {
        uint64_t id = 0, sent = 0;
        uint16_t symbol = 0;
        Side side = Side::Buy;
        int64_t remaining = 0, limit = 0; // limit=0: market IOC on next eligible snapshot
    };
public:
    Engine(Settings settings, size_t sample_capacity, size_t fill_capacity)
        : cfg_(settings), cash_(settings.initial_cash), fills_(fill_capacity), samples_(sample_capacity) {
        if (!std::isfinite(cash_) || cash_ <= 0 || cash_ > 1e15 ||
            !std::isfinite(cfg_.commission_bps) || cfg_.commission_bps < 0 || cfg_.commission_bps > 1000 ||
            !std::isfinite(cfg_.slippage_bps) || cfg_.slippage_bps < 0 || cfg_.slippage_bps > 1000 ||
            cfg_.max_position <= 0 || cfg_.max_position > kMaxQty)
            throw std::invalid_argument("invalid replay settings");
    }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    const Book& book(uint16_t symbol) const noexcept { return books_[symbol < kSymbols ? symbol : kSymbols]; }
    const Position& position(uint16_t symbol) const noexcept { return positions_[symbol < kSymbols ? symbol : kSymbols]; }
    uint64_t book_timestamp(uint16_t symbol) const noexcept { return symbol < kSymbols ? book_times_[symbol] : 0; }
    double cash() const noexcept { return cash_; }
    double equity() const noexcept {
        double result = cash_;
        for (size_t s = 0; s < kSymbols; ++s) result += positions_[s].qty * (marks_[s] / 100.0);
        return result;
    }
    bool pending(uint16_t symbol) const noexcept {
        for (const auto& o : orders_) if (o.remaining && o.symbol == symbol) return true;
        return false;
    }
    // 0 means rejected. No short selling or leverage; risk rechecked at fill.
    uint64_t submit(uint16_t symbol, Side side, int64_t qty, int64_t limit = 0) noexcept {
        if (failed_ || !started_ || symbol >= kSymbols || (side != Side::Buy && side != Side::Sell) ||
            qty <= 0 || qty > cfg_.max_position || limit < 0 || limit > kMaxPrice ||
            cfg_.latency_ns > UINT64_MAX - now_) { ++rejections_; return 0; }
        for (auto& o : orders_) if (!o.remaining) {
            o = {++next_id_, now_, symbol, side, qty, limit};
            return o.id;
        }
        ++rejections_; return 0;
    }
    bool cancel(uint64_t id) noexcept {
        for (auto& o : orders_) if (o.id == id && o.remaining) { o.remaining = 0; return true; }
        return false;
    }
    size_t fill_count() const noexcept { return nf_; }
    size_t sample_count() const noexcept { return ns_; }
    const Fill& fill(size_t i) const noexcept { return fills_[i]; }
    const Sample& sample(size_t i) const noexcept { return samples_[i]; }
    uint64_t rejections() const noexcept { return rejections_; }
    const char* error() const noexcept { return failed_; }
    static bool valid(const Event& e) noexcept {
        if (!e.timestamp || e.symbol >= kSymbols || (e.kind != Kind::Open && e.kind != Kind::Bar && e.kind != Kind::Quote)) return false;
        auto price = [](int64_t p) { return p > 0 && p <= kMaxPrice; };
        if (e.kind == Kind::Bar)
            return price(e.open) && price(e.close) && price(e.low) && price(e.high) &&
                e.low <= std::min(e.open, e.close) && e.high >= std::max(e.open, e.close) &&
                e.volume >= 0 && e.volume <= kMaxQty;
        for (size_t i = 0; i < kDepth; ++i) {
            const auto b = e.book.bids[i], a = e.book.asks[i];
            if (b.qty < 0 || a.qty < 0 || b.qty > kMaxQty || a.qty > kMaxQty ||
                (b.price != 0 && !price(b.price)) || (a.price != 0 && !price(a.price)) ||
                (!b.price && b.qty) || (!a.price && a.qty)) return false;
            if (i && ((b.price && (!e.book.bids[i-1].price || b.price >= e.book.bids[i-1].price)) ||
                      (a.price && (!e.book.asks[i-1].price || a.price <= e.book.asks[i-1].price)))) return false;
        }
        return price(e.book.bids[0].price) && price(e.book.asks[0].price) && e.book.bids[0].price <= e.book.asks[0].price;
    }
    // Events must be globally sorted. All same-timestamp orders wait for a
    // strictly later snapshot, preventing symbol ordering from granting fills.
    bool step(const Event& e, Strategy& strategy) noexcept {
        if (failed_) return false;
        if (!valid(e) || (started_ && e.timestamp < now_)) return fail("invalid or unsorted event");
        if (ns_ == samples_.size()) return fail("equity capacity exhausted");
        now_ = e.timestamp; started_ = true;
        if (e.kind != Kind::Bar) {
            books_[e.symbol] = e.book;
            book_times_[e.symbol] = now_;
            marks_[e.symbol] = (e.book.bids[0].price + e.book.asks[0].price) / 2;
            // Bounded priority index: market first, then aggressive price, then
            // submission sequence. Buy and sell consume separate quoted sides.
            std::array<size_t, kOrders> indices{};
            size_t count = 0;
            for (size_t i = 0; i < kOrders; ++i) {
                const auto& o = orders_[i];
                if (o.remaining && o.symbol == e.symbol && o.sent < now_ && now_ - o.sent >= cfg_.latency_ns)
                    indices[count++] = i;
            }
            std::sort(indices.begin(), indices.begin() + count, [&](size_t x, size_t y) {
                const auto& a = orders_[x]; const auto& b = orders_[y];
                if (a.side != b.side) return a.side < b.side;
                if ((!a.limit) != (!b.limit)) return !a.limit;
                if (a.limit != b.limit) return a.side == Side::Buy ? a.limit > b.limit : a.limit < b.limit;
                return a.id < b.id;
            });
            for (size_t i = 0; i < count; ++i) {
                auto& o = orders_[indices[i]];
                if (!o.remaining) continue; // fill callback may cancel another order
                auto& levels = o.side == Side::Buy ? books_[e.symbol].asks : books_[e.symbol].bids;
                for (auto& level : levels) {
                    if (!o.remaining || !level.qty) continue;
                    const auto slip = static_cast<int64_t>(std::ceil(level.price * cfg_.slippage_bps / 10000.0));
                    const int64_t px = level.price + (o.side == Side::Buy ? slip : -slip);
                    if (px <= 0 || (o.limit && (o.side == Side::Buy ? px > o.limit : px < o.limit))) continue;
                    auto& p = positions_[e.symbol];
                    int64_t qty = std::min(o.remaining, level.qty);
                    if (o.side == Side::Buy) {
                        qty = std::min(qty, cfg_.max_position - p.qty);
                        const double unit = px / 100.0 * (1 + cfg_.commission_bps / 10000.0);
                        qty = std::min(qty, static_cast<int64_t>(std::floor(std::max(0.0, cash_) / unit)));
                    } else qty = std::min(qty, p.qty);
                    if (qty <= 0) { ++rejections_; o.remaining = 0; break; }
                    if (nf_ == fills_.size()) return fail("fill capacity exhausted");
                    Fill f{now_, o.id, e.symbol, o.side, qty, px};
                    const double notional = qty * (px / 100.0);
                    f.commission = notional * cfg_.commission_bps / 10000.0;
                    f.slippage = qty * (slip / 100.0);
                    if (o.side == Side::Buy) {
                        cash_ -= notional + f.commission;
                        p.cost += notional + f.commission; p.qty += qty;
                    } else {
                        const double basis = p.cost * (static_cast<double>(qty) / p.qty);
                        f.closed_qty = qty; f.realized = notional - f.commission - basis;
                        cash_ += notional - f.commission;
                        p.cost -= basis; p.qty -= qty; p.realized += f.realized;
                        if (!p.qty) p.cost = 0;
                    }
                    fees_ += f.commission; slippage_ += f.slippage;
                    level.qty -= qty; o.remaining -= qty; fills_[nf_++] = f;
                    // Defer callbacks until matching ends, so slot reuse cannot
                    // accidentally execute a newly submitted order in this snapshot.
                }
                if (!o.limit) o.remaining = 0;
            }
        } else marks_[e.symbol] = e.close;
        while (notified_ < nf_) strategy.on_fill(fills_[notified_++], *this);
        if (e.kind != Kind::Open) strategy.on_market(e, *this);
        samples_[ns_++] = {now_, cash_, equity(), fees_, slippage_};
        return true;
    }
private:
    bool fail(const char* message) noexcept { failed_ = message; return false; }
    Settings cfg_;
    double cash_, fees_ = 0, slippage_ = 0;
    std::array<Book, kSymbols + 1> books_{};
    std::array<Position, kSymbols + 1> positions_{};
    std::array<int64_t, kSymbols> marks_{};
    std::array<uint64_t, kSymbols> book_times_{};
    std::array<Order, kOrders> orders_{};
    std::vector<Fill> fills_;
    std::vector<Sample> samples_;
    size_t nf_ = 0, ns_ = 0, notified_ = 0;
    uint64_t now_ = 0, next_id_ = 0, rejections_ = 0;
    bool started_ = false;
    const char* failed_ = nullptr;
};

// Long-only rolling mean reversion or momentum; independently stateful symbols.
class RollingStrategy final : public Strategy {
public:
    RollingStrategy(size_t lookback, double threshold, int64_t quantity, bool momentum = false)
        : lookback_(lookback), threshold_(threshold), quantity_(quantity), momentum_(momentum) {
        if (lookback < 2 || lookback > 256 || !std::isfinite(threshold) || threshold <= 0 || threshold >= 1 ||
            quantity <= 0 || quantity > kMaxQty) throw std::invalid_argument("invalid strategy parameters");
    }
    void on_market(const Event& e, Engine& engine) noexcept override {
        const size_t s = e.symbol;
        const double px = e.kind == Kind::Bar ? e.close : (e.book.bids[0].price + e.book.asks[0].price) / 2.0;
        if (counts_[s] >= lookback_ && !engine.pending(e.symbol)) {
            const double mean = sums_[s] / lookback_;
            const bool enter = momentum_ ? px > mean * (1 + threshold_) : px < mean * (1 - threshold_);
            const bool leave = momentum_ ? px < mean : px >= mean;
            const auto held = engine.position(e.symbol).qty;
            if (!held && enter) engine.submit(e.symbol, Side::Buy, quantity_);
            else if (held && leave) engine.submit(e.symbol, Side::Sell, held);
        }
        size_t& index = indices_[s];
        sums_[s] -= history_[s][index]; history_[s][index] = px; sums_[s] += px;
        index = (index + 1) % lookback_; ++counts_[s];
    }
private:
    size_t lookback_; double threshold_; int64_t quantity_; bool momentum_;
    std::array<std::array<double, 256>, kSymbols> history_{};
    std::array<double, kSymbols> sums_{};
    std::array<size_t, kSymbols> counts_{}, indices_{};
};
} // namespace luv::historical
