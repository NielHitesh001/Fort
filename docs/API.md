# Fort Core API Reference

This document provides documentation for core classes, method signatures, memory guarantees, and thread-safety models across the Fort simulation framework.

---

## 1. Memory Management: `luv::Arena` (`luv_arena.hpp`)

Pre-allocated contiguous memory region eliminating runtime dynamic allocations (`malloc`/`new`).

```cpp
namespace luv {
class Arena {
public:
    // Initialize contiguous memory block (with optional mlock)
    bool init(size_t capacity_bytes = 64 * 1024 * 1024) noexcept;

    // Allocate memory aligned to specified boundary (e.g. 64-byte cache line)
    void* allocate(size_t size, size_t alignment = 64) noexcept;

    // Reset allocation offset (invalidates prior allocations)
    void reset() noexcept;

    // Query remaining capacity
    size_t remaining_bytes() const noexcept;
};
}
```
- **Thread Safety**: Single-writer or externally synchronized.
- **Failure Mode**: Returns `nullptr` upon capacity exhaustion (no heap fallback).

---

## 2. Order Book Engine: `luv::LimitOrderBook` (`luv_lob.hpp`)

In-memory limit order matching engine.

```cpp
namespace luv {
class LimitOrderBook {
public:
    // Initialize book with backing arena storage
    bool init(Arena& arena) noexcept;

    // Insert limit order
    bool insert_order(const Order& order) noexcept;

    // Cancel existing order by ID
    bool cancel_order(uint64_t order_id) noexcept;

    // Top-of-book queries
    PriceLevel best_bid() const noexcept;
    PriceLevel best_ask() const noexcept;

    // Multi-level depth extraction
    size_t get_bid_depth(PriceLevel* out_levels, size_t max_levels) const noexcept;
    size_t get_ask_depth(PriceLevel* out_levels, size_t max_levels) const noexcept;
};
}
```
- **Thread Safety**: Single-writer (feed decoder thread) + concurrent readers.
- **Complexity**: $O(\log N)$ for price-level insertion; $O(1)$ for cancel by ID via hash map.

---

## 3. Protocol Parsing: `luv::ITCHDecoder` (`luv_decode_itch.hpp`)

Nasdaq ITCH 5.0 binary message parser.

```cpp
namespace luv {
class ITCHDecoder {
public:
    // Decode incoming binary packet
    bool decode_message(const uint8_t* buffer, size_t length, ITCHMessage& out_msg) noexcept;
};
}
```
- **Supported Messages**: System Event ('S'), Stock Directory ('R'), Trading Action ('H'), Add Order ('A'/'F'), Order Executed ('E'/'C'), Order Cancel ('X'/'D'), Trade ('P'/'Q'), NOII ('I').
- **Input Validation**: Strict buffer length verification, message type validation, and numeric range bounds checking.

---

## 4. Quantitative Pricing: `luv::HestonOptionPricer` / `luv::BatesOptionPricer`

Option pricing via characteristic function quadrature.

```cpp
namespace luv {
struct BatesParameters {
    double spot_price{100.0};
    double strike_price{100.0};
    double risk_free_rate{0.03};
    double dividend_yield{0.0};
    double time_to_maturity{1.0};
    double initial_variance{0.04};
    double mean_reversion_kappa{2.0};
    double long_term_var_theta{0.04};
    double vol_of_vol_xi{0.30};
    double correlation_rho{-0.70};
    double jump_intensity_lambda{0.10};
    double jump_mean_gamma{-0.05};
    double jump_vol_delta{0.15};
};

class BatesOptionPricer {
public:
    // Prices European Call & Put and computes analytical Greeks
    static BatesPriceResult price_european_option(const BatesParameters& params) noexcept;
};
}
```
- **Memory Allocation**: Zero heap allocations; evaluates via fixed 64-point Gauss-Legendre quadrature.
- **Guarantees**: Obeys exact Put-Call Parity: $C - P = S_0 e^{-q T} - K e^{-r T}$.

---

## 5. Telemetry & Metrics: `luv::Telemetry` (`luv_telemetry.hpp`)

Lock-free single-producer single-consumer (SPSC) ring buffer telemetry.

```cpp
namespace luv {
class Telemetry {
public:
    // Record execution latency in nanoseconds
    void record_latency_ns(uint64_t latency_ns) noexcept;

    // Percentile statistics
    uint64_t percentile(double p) const noexcept; // e.g. 50.0, 99.0, 99.9

    // Cumulative summary
    uint64_t total_samples() const noexcept;
    uint64_t min_latency_ns() const noexcept;
    uint64_t max_latency_ns() const noexcept;
};
}
```
