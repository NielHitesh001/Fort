# LUV---Flicker-

Low-latency market microstructure engine for Nasdaq equity trading. The repository includes a simulation feed, ITCH decoding, an in-memory limit order book, execution/risk controls, and optional DPDK integration.

## Features

- **DPDK-based packet processing** — kernel bypass for ultra-low-latency network I/O (polling mode drivers, hugepages)
- **Packet I/O abstraction** — macOS and default CI builds use a no-DPDK stub; Linux can opt into the real DPDK backend with `-DLUV_ENABLE_DPDK=ON`
- **Multi-Protocol Market Data Decoders** — polymorphic decoder boundary supporting Nasdaq TotalView-ITCH 5.0 and Simple Binary Encoding (SBE)
- **Multi-Asset Precision & Metadata** — Instrument registry supporting Equity, Crypto, FX, and Futures decimal conversions
- **Pre-allocated limit order book** — in-memory order matching for equity instruments
- **Memory arena allocator & Sharded Rings** — pre-allocated memory pool, SPSC ring occupancy monitoring, and deterministic parallel symbol stream sharding
- **Execution engine & Order Types** — supports IOC, FOK, Day, GTC, Cancel/Replace (`'U'`), Venue Rejections (`'J'`), and in-memory Stop/Pegged triggers
- **Multi-Tier Circuit Breakers & Dynamic Risk** — Closed/Open/Half-Open state transitions, burst rate limiting, consecutive rejection limits, gross loss thresholds, and granular per-symbol halting
- **SEC 17a-4 / WORM Audit Trail** — file-locked append-only log with periodic manifest checkpointing, cryptographic root hash verification, range queries, and JSON compliance export
- **Telemetry & observability** — performance metrics collection (latency histograms, throughput, resource usage)
- **Simulation mode** — test harness with synthetic market data feed (no DPDK/network required)

## Architecture

```
Network (Nasdaq ITCH / SBE feed)
         ↓
   DPDK Feed Handler (kernel bypass)
         ↓
   Multi-Protocol Feed Decoder (ITCH 5.0 / SBE)
         ↓
  Limit Order Book (pre-allocated, reader-writer synchronized)
         ↓
   Execution Gateway (TIF, Stop/Pegged triggers, Risk & Circuit Breakers)
         ↓
   Telemetry & SEC 17a-4 WORM Audit Trail
```

### Core Components

| Module | Purpose | Thread Model |
|--------|---------|--------------|
| `luv_feed.hpp` | Base feed interface | N/A (abstract) |
| `luv_decoder.hpp` | Multi-protocol decoders (ITCH, SBE) & instrument metadata | Stateless / Single thread |
| `luv_feed_dpdk.hpp` | DPDK packet processor | Single consumer thread, polling mode |
| `luv_feed_sim.hpp` | Synthetic market feed | Single thread, simulated time |
| `luv_decode_itch.hpp` | ITCH 5.0 binary decoder | Single thread (called by feed handler) |
| `luv_lob.hpp` | Limit order book | Reader-writer lock (readers = data consumers, writer = ITCH decoder) |
| `luv_execution.hpp` | Order execution, TIF, Stop/Pegged triggers & risk gateway | Single thread, enqueued mutations from LOB |
| `luv_safety.hpp` | Multi-tier circuit breaker & SEC 17a-4 WORM audit log | Thread-safe atomic state transitions |
| `luv_arena.hpp` | Pre-allocated memory pool & Sharded SPSC rings | Thread-safe up to pre-allocated size |
| `luv_telemetry.hpp` | Performance metrics | Lock-free ring buffer for event recording |
| `luv_consumer.hpp` | Generic data consumer interface | N/A (abstract) |
| `luv_features.hpp` | Feature flags & configuration | Read-only after startup |

## Thread Safety Model

**Single-threaded event loop design**:

1. **ITCH decoder thread** (writer): sole mutator of the LOB
2. **LOB**: the ITCH decoder is the sole caller that mutates or processes the book. Query accessors are read-only but must not run concurrently with mutation unless the application supplies synchronization.
3. **Execution engine** (single-threaded): enqueued from LOB mutations, transmits orders
4. **Telemetry** (lock-free): ring buffer; no blocking on critical path
5. **Arena allocator**: pre-allocated; all allocations must fit or the system fails fast (no heap fragmentation)

**No unbounded heap allocations** on the critical path. All data structures use the arena allocator.

The LOB itself is not lock-free: mutations take an exclusive reader-writer
lock, while query accessors take a shared lock where required by the API.

The tick and telemetry queues are strict SPSC rings: exactly one producer may
claim/commit and exactly one consumer may peek/consume each ring. Build with
`-DLUV_REQUIRE_MLOCK=ON` for production deployments where failure to lock the
infrastructure arena into RAM must abort startup; the default is best-effort
locking for simulation environments.

## Build

### Dependencies

- **C++20 compiler** (recent Clang or GCC)
- **DPDK 21.11+** (for production DPDK feed; optional if using simulation)
- **CMake 3.20+**

The DPDK backend is optional. On macOS, DPDK headers are never included and
`SimFeedSource` is the supported feed for local execution. On Linux, configure
with `-DLUV_ENABLE_DPDK=ON` and install a `libdpdk` pkg-config package to build
the native packet backend.

### Quickstart

```bash
# Configure and build the simulation engine (no DPDK or network required)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run the default test suite
ctest --test-dir build --output-on-failure

# Run the simulation engine
./build/luv_engine --messages 100000

# Remove generated build files
rm -rf build
```

### Environment Variables

```bash
export DPDK_ROOT=/usr/local/dpdk           # DPDK installation directory
export ITCH_FEED=dpdk                      # 'dpdk' or 'sim'
export LOB_PREALLOC_SIZE=1048576           # Arena allocator size (bytes)
export EXECUTION_MODE=live                 # 'live' or 'paper'
```

## Usage

### Basic Example: Simulation

```cpp
#include "luv_feed_sim.hpp"
#include "luv_lob.hpp"
#include "luv_execution.hpp"

int main() {
    // Create components
    luv::Arena arena;
    if (!arena.init()) return 1;

    luv::SimConfig feed_config{};
    luv::SimFeedSource feed(feed_config);
    if (!feed.init(arena)) return 1;

    luv::Consumer consumer;
    luv::ExecutionGateway execution;
    if (!consumer.init(arena) || !execution.init(arena)) return 1;

    // Run simulation
    while (feed.poll()) {
        // Feed consumes ITCH events and mutates LOB
        // Execution engine processes mutations
        // Telemetry records latencies
    }

    return 0;
}
```

### Monitoring Telemetry

```cpp
auto latency_p50 = telemetry.latency_percentile(50);
auto latency_p99 = telemetry.latency_percentile(99);
auto throughput = telemetry.events_per_second();

std::cout << "Order processing: p50=" << latency_p50 << "us, p99=" 
          << latency_p99 << "us, throughput=" << throughput << " evt/s\n";
```

### Paper-Trading Blitz

The staging runner exercises pre-trade risk, telemetry, simulated fills, and
recovery-ledger replay without connecting to an exchange. Its volume and pace
are explicit so a long run can be reproduced and its ledger can be retained:

```bash
./build/luv_staging_runner \
  --orders 1728000 \
  --rate-hz 10 \
  --ledger /tmp/luv_staging_48h_$(date +%Y%m%d).bin
```

This represents 48 hours at 10 orders per second. Use `--rate-hz 0` for a
maximum-throughput smoke test. Review the emitted telemetry and replay result
after the run; this harness is synthetic validation, not live or regulatory
approval.

For the investor-facing scope, evidence boundaries, and open diligence items,
see [INVESTOR_READINESS_BRIEF.md](INVESTOR_READINESS_BRIEF.md).

## Performance

The repository contains `luv_latency_benchmark`, but no benchmark results are
published here yet. Latency depends heavily on compiler, CPU, kernel, NUMA
placement, feed configuration, and whether DPDK is enabled. Run the benchmark
on the target host. Its output includes the compiler, operating system, host
architecture, and iteration count; record that output with the commit and feed
configuration before using measurements for capacity planning. These numbers
measure local simulated LOB adds, not a DPDK feed-to-execution round trip.

```bash
./build/luv_latency_benchmark
```

## Deployment

### Production Checklist

- [ ] Arena allocator sized for peak order count + 30% headroom
- [ ] DPDK hugepages configured and reserved
- [ ] CPU affinity pinning enabled for feed + execution threads
- [ ] Telemetry output wired to monitoring system
- [ ] Kill switch (circuit breaker) integrated
- [ ] Order validation + risk limits enforced upstream
- [ ] Audit logging of all executions enabled
- [ ] Failover/redundancy strategy documented

### Known Limitations

1. **Single-machine deployment** — no built-in clustering or failover
2. **Pre-allocation is hard limit** — LOB and arena cannot grow beyond configured size
3. **ITCH only** — other market data formats not supported
4. **Equity instruments only** — no derivatives, crypto, commodities
5. **Order types** — market and limit only; no conditional logic

### Failure Modes

| Condition | Behavior | Recovery |
|-----------|----------|----------|
| Arena exhausted | Fast fail, no new orders accepted | Restart (requires reload) |
| ITCH feed stall | LOB becomes stale; execution blocked | Automatic reconnect (see config) |
| Execution transmission timeout | Order marked as failed; logged | Manual intervention required |

## Configuration

Create a config file (example: `config.json`):

```json
{
  "arena": {
    "size_bytes": 10485760,
    "alignment": 64
  },
  "dpdk": {
    "enabled": true,
    "nic_port": 0,
    "queue_depth": 256,
    "hugepages_2mb": 128
  },
  "itch": {
    "multicast_addr": "239.1.1.1",
    "port": 14310,
    "interface": "eth0"
  },
  "execution": {
    "mode": "paper",
    "max_order_size": 1000000,
    "transmission_timeout_us": 500
  },
  "telemetry": {
    "enabled": true,
    "ring_size": 1048576,
    "export_interval_ms": 1000
  }
}
```

## AI Component (luv_ai.hpp)

**Status: Research prototype, not used in production path.**

This module explores machine-learning-based order prediction and execution optimization. It is **disabled by default** and should **not be enabled in live trading** without extensive validation.

Current capabilities:
- Experimental latency prediction model
- Prototype execution timing optimizer
- Research-only; no guarantees on correctness or safety

Dynamic shared-object model loading is disabled by default. Enable it only for
trusted model artifacts with:
```bash
cmake -S . -B build -DLUV_ENABLE_DYNAMIC_MODEL_LOADING=ON
```

## Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Individual test suites
./build/luv_arena        # Memory allocator tests
./build/luv_lob          # Order book correctness
./build/luv_feed         # Feed processing (simulation)
./build/luv_execution    # Order routing
./build/luv_stress       # Load & concurrency
./build/luv_ai_telemetry # Telemetry + AI integration
```

CTest reports pass/fail status for each executable. CI runs a bounded 250,000
message stress test and an AddressSanitizer/UndefinedBehaviorSanitizer build.
The default local configuration skips the stress test; enable it with
`-DLUV_ENABLE_LONG_STRESS_TESTS=ON`. Neither test claims race-freedom or a
fixed latency; use ThreadSanitizer and target-host benchmarks separately when
investigating those properties.

## Roadmap

- [ ] Multi-instrument orderbook sharding
- [ ] Adaptive hugepage sizing
- [ ] gRPC telemetry export
- [ ] Kubernetes deployment templates
- [ ] SEC 17a-4 audit logging compliance
- [ ] Circuit breaker integration

## License

MIT License. See `LICENSE` file.

## Contributing

Pull requests welcome. Please include:
1. Test coverage for any new components
2. Latency impact analysis (P50/P99 before/after)
3. Memory footprint impact
4. Thread safety audit for concurrency changes

## Support

For issues, questions, or deployment guidance: open a GitHub issue.

---

**Disclaimer:** This is a trading system framework. Use at your own risk. Thoroughly test before deploying with real capital. No warranties, express or implied.
