#include "luv_historical_backtest.hpp"
#include "luv_historical_lob_adapter.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>
#include <cstdio>
using namespace luv::historical;
static bool watch = false;
static size_t allocations = 0;
void* operator new(std::size_t n) { if (watch) ++allocations; if (auto p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
struct Idle : Strategy { void on_market(const Event&, Engine&) noexcept override {} };
Event quote(uint64_t t, uint16_t s=0, int64_t bid=10000, int64_t ask=10000, int64_t size=10) {
    Event e{}; e.timestamp=t; e.symbol=s; e.book.bids[0]={bid,size}; e.book.asks[0]={ask,size}; return e;
}
void liquidity_and_accounting() {
    Engine e({10000, 10, 0, 1000, 0}, 20, 20); Idle s;
    assert(e.step(quote(1),s));
    const auto a=e.submit(0,Side::Buy,8,10100), b=e.submit(0,Side::Buy,8,10100);
    assert(a && b);
    assert(e.step(quote(2,1),s)); assert(e.fill_count()==0);
    assert(e.step(quote(3),s)); assert(e.fill_count()==2);
    assert(e.fill(0).qty==8 && e.fill(1).qty==2 && e.position(0).qty==10);
    assert(std::abs(e.equity()-9999)<1e-8); // inventory is not a loss
    assert(e.cancel(b));
    assert(e.submit(0,Side::Sell,10));
    assert(e.step(quote(4,0,11000,11000),s));
    assert(e.position(0).qty==0 && std::abs(e.equity()-10097.9)<1e-8);
    assert(std::abs(e.fill(2).realized-97.9)<1e-8);
}
void timing_and_limits() {
    Engine e({10000,0,100,1000,2},20,20); Idle s;
    assert(e.step(quote(1),s));
    auto id=e.submit(0,Side::Buy,5,10000);
    assert(e.step(quote(2),s)); assert(e.fill_count()==0);
    assert(e.step(quote(3),s)); assert(e.fill_count()==0); // slippage cannot violate limit
    assert(e.cancel(id));
    e.submit(0,Side::Buy,20);
    assert(e.step(quote(5),s)); assert(e.fill_count()==1 && e.fill(0).qty==10);
    assert(!e.pending(0)); // market IOC cancels remainder
    assert(e.fill(0).price==10100);
    e.submit(0,Side::Sell,1);
    assert(e.step(quote(5),s)); assert(e.fill_count()==1); // same timestamp forbidden
}
void risk_priority_and_depth() {
    Engine e({500,0,0,100,0},20,20); Idle s;
    assert(e.step(quote(1),s));
    assert(!e.submit(kSymbols,Side::Buy,1)); assert(!e.submit(0,Side::Buy,-1));
    e.submit(0,Side::Sell,2); e.submit(0,Side::Buy,5,10000);
    const auto market=e.submit(0,Side::Buy,5);
    assert(e.step(quote(2),s));
    // Market buy first on buy side; total affordable shares = 5. Sell can
    // subsequently close only owned shares; position never becomes negative.
    assert(e.fill(0).order_id==market && e.position(0).qty>=0 && e.cash()>=0);
    Engine d({10000,0,0,100,0},5,10); assert(d.step(quote(1),s));
    d.submit(0,Side::Buy,15);
    auto q=quote(2); q.book.asks[1]={10100,10};
    assert(d.step(q,s)); assert(d.fill_count()==2 && d.fill(1).price==10100 && d.fill(1).qty==5);
}
void failure_and_no_allocations() {
    Idle s; Engine e({10000,0,0,100,0},10,1);
    assert(e.step(quote(1),s)); e.submit(0,Side::Buy,1); assert(e.step(quote(2),s));
    e.submit(0,Side::Buy,1); assert(!e.step(quote(3),s)); assert(e.error());
    Engine invalid({},2,2); auto q=quote(2); q.book.bids[0].price=11000;
    assert(!invalid.step(q,s));
    Engine order({},3,3); assert(order.step(quote(2),s)); assert(!order.step(quote(1),s));
    Engine alloc({},100,100); RollingStrategy strat(2,.01,1);
    watch=true;
    for(uint64_t i=1;i<=100;++i) assert(alloc.step(quote(i,0,(i%4<2?9000:11000),(i%4<2?9000:11000)),strat));
    watch=false; assert(allocations==0);
}
void determinism() {
    Engine a({},100,100),b({},100,100); RollingStrategy x(2,.01,1),y(2,.01,1);
    for(uint64_t i=1;i<=100;++i) { auto q=quote(i,0,10000+(i%3)*1000,10000+(i%3)*1000); assert(a.step(q,x)); assert(b.step(q,y)); }
    assert(a.fill_count()==b.fill_count()); assert(a.equity()==b.equity());
    for(size_t i=0;i<a.fill_count();++i) { assert(a.fill(i).timestamp==b.fill(i).timestamp); assert(a.fill(i).price==b.fill(i).price); }
}
void bars_and_callback_orders() {
    struct Reactive : Strategy {
        void on_market(const Event& e, Engine& engine) noexcept override {
            if (e.kind == Kind::Bar && !engine.position(0).qty) engine.submit(0, Side::Buy, 1);
        }
        void on_fill(const Fill& f, Engine& engine) noexcept override {
            if (f.side == Side::Buy) engine.submit(0, Side::Sell, 1);
        }
    } strategy;
    Engine e({10000,0,0,100,0},10,10);
    Event bar{}; bar.timestamp=1; bar.kind=Kind::Bar;
    bar.open=bar.high=bar.low=bar.close=10000; bar.volume=5'000'000'000;
    assert(Engine::valid(bar));
    assert(e.step(bar,strategy)); assert(e.fill_count()==0 && e.pending(0));
    auto q=quote(2); q.kind=Kind::Open;
    assert(e.step(q,strategy)); assert(e.fill_count()==1 && e.position(0).qty==1);
    assert(e.book_timestamp(0)==2);
    assert(e.step(quote(3),strategy)); assert(e.fill_count()==2 && e.position(0).qty==0);
    Idle idle; Engine capacity({},600,600);
    assert(capacity.step(quote(1),idle));
    for (size_t i=0;i<kOrders;++i) assert(capacity.submit(0,Side::Buy,1,9000));
    assert(!capacity.submit(0,Side::Buy,1));
    assert(capacity.cancel(1)); assert(capacity.submit(0,Side::Buy,1));
    // Reuse completed slots for more lifetime orders than the fixed capacity.
    Engine reuse({},600,600); assert(reuse.step(quote(1),idle));
    for (uint64_t i=2;i<550;++i) { assert(reuse.submit(0, i%2 ? Side::Sell : Side::Buy,1)); assert(reuse.step(quote(i),idle)); }
    assert(reuse.fill_count()==548);
}
void broad_universe() {
    Engine engine({1'000'000,0,0,100,0},1000,1000); Idle strategy;
    for (uint16_t symbol=0;symbol<500;++symbol) assert(engine.step(quote(1,symbol),strategy));
    for (uint16_t symbol=0;symbol<500;++symbol) assert(engine.submit(symbol,Side::Buy,1));
    watch=true;
    for (uint16_t symbol=0;symbol<500;++symbol) assert(engine.step(quote(2,symbol),strategy));
    watch=false;
    assert(allocations==0 && engine.fill_count()==500 && engine.rejections()==0);
    assert(engine.position(499).qty==1 && engine.equity()==1'000'000);
}
int main(){broad_universe();bars_and_callback_orders();liquidity_and_accounting();timing_and_limits();risk_priority_and_depth();failure_and_no_allocations();determinism(); std::puts("Historical replay tests passed; zero replay allocations.");}
