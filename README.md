# Fort: In-Process Limit Order Book Research Simulator

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.20+-green.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Status: Parked](https://img.shields.io/badge/Status-PARKED-lightgrey.svg)](#)

---

> [!IMPORTANT]
> **Status: PARKED (research simulator).**  
> **What Fort is:** Fort is a high-performance **in-process** limit-order-book **simulator** designed for market microstructure research, algorithmic execution study, and low-latency C++ systems benchmarking.  
> **What Fort is NOT:** Fort is **not** a live trading venue, broker-dealer, exchange gateway, Bloomberg terminal, managed money platform, or certified regulatory compliance system. It does not trade live capital or connect to live exchange sessions.  
> **Maintenance Notice:** **No active feature development.** This project is frozen in a stable research state. Issues and pull requests may go unanswered unless the owner un-parks the repository for a specific written project goal. See [**docs/PARKED.md**](docs/PARKED.md).  
> **Key References:** [**docs/CAPABILITIES.md**](docs/CAPABILITIES.md) · [**docs/KNOWN_ISSUES.md**](docs/KNOWN_ISSUES.md) · [**docs/ARCHITECTURE_AND_SCOPE.md**](docs/ARCHITECTURE_AND_SCOPE.md) · [**docs/RESEARCH_MODULES.md**](docs/RESEARCH_MODULES.md).

---

## Engine Path Quick Start

The authoritative, supported build artifact is the core simulation engine (`luv_engine`) and its supporting tests.

- **Primary host:** macOS (Clang / AppleClang)
- **Secondary host:** Linux / Ubuntu 24.04 (Clang / GCC)

### 1. Prerequisites
- Modern C++20 compiler (Clang 13+, GCC 11+, or AppleClang)
- CMake 3.20+
- OpenSSL (crypto library for hash chaining and auth)

### 2. Configure, Build & Run (Engine Path Only)

```bash
# Clone the repository
git clone https://github.com/NielHitesh001/Fort.git
cd Fort

# Configure release build (defaults to laptop-friendly 64MB AI arena and 127.0.0.1 HTTP bind)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build core simulation engine and control-plane modules
cmake --build build --target luv_engine luv_websocket luv_http_server luv_prometheus_export --parallel

# Run core engine tests
ctest --test-dir build -R '^(feed|lob|execution|sequence_tracker|crash_recovery|numeric_limits|arena|websocket|http_server|prometheus_export)$' --output-on-failure

# Run the simulation engine with synthetic feed
./build/luv_engine --messages 100000
```

For sanitizer configurations (ASan/UBSan) and optional build definitions, see [**BUILDING.md**](BUILDING.md).

---

## Core Capabilities (Simulation Scope)

- **Binary Feed Decoder**: Nasdaq ITCH 5.0 binary protocol parser with strict input length and bounds validation.
- **In-Memory Limit Order Book (LOB)**: Cache-aligned price-time FIFO matching engine supporting limit, market, pegged, and iceberg orders.
- **Zero-Allocation Memory Arena**: Pre-allocated contiguous memory pools (`luv_arena.hpp`) eliminating heap fragmentation and dynamic runtime allocation on critical paths.
- **Microstructure Analytics**: Real-time Order Flow Imbalance (OFI), micro-price estimators, and live decision tree inference.
- **Pre-Trade Risk Gateway**: Authoritative simulation risk check (`PreTradeRisk` in `luv_execution.hpp`) enforcing symbol position limits, fat-finger collars, and circuit breakers.
- **Local Control Plane**: Loopback HTTP (`127.0.0.1`) and WebSocket interface for local simulation control, injection, and fill observation.
- **Research Library**: Standalone quantitative derivatives pricing models and regulatory capital formulas retained under `LUV_TESTS` for academic study (see [**docs/RESEARCH_MODULES.md**](docs/RESEARCH_MODULES.md)).

---

## Performance & Benchmark Disclaimers

Fort includes local microbenchmarks for studying systems programming techniques. Local computation timings do not establish feed-to-exchange latency or any service-level guarantee.

- Benchmarks run on local test hardware and measure in-memory compute steps only.
- WebSocket latency figures in CI macOS runs are **report-only diagnostics**, not production SLAs.
- See [**docs/BENCHMARKING.md**](docs/BENCHMARKING.md), [**docs/PERFORMANCE_AUDIT_FINDINGS.md**](docs/PERFORMANCE_AUDIT_FINDINGS.md), and [**docs/KNOWN_ISSUES.md**](docs/KNOWN_ISSUES.md).

---

## Documentation Index

- [**docs/PARKED.md**](docs/PARKED.md) - Project freeze rationale, supported paths, and un-parking conditions.
- [**docs/KNOWN_ISSUES.md**](docs/KNOWN_ISSUES.md) - Current limitations, book capacity limits, and parked status.
- [**docs/CAPABILITIES.md**](docs/CAPABILITIES.md) - Full specification of included vs. excluded capabilities.
- [**docs/RESEARCH_MODULES.md**](docs/RESEARCH_MODULES.md) - Index of standalone research and academic test targets.
- [**docs/ARCHITECTURE_AND_SCOPE.md**](docs/ARCHITECTURE_AND_SCOPE.md) - Runtime boundaries and architectural layers.
- [**docs/COMPLIANCE_STATUS.md**](docs/COMPLIANCE_STATUS.md) - Regulatory simulation status matrix.
- [**docs/ARCHITECTURE.md**](docs/ARCHITECTURE.md) - System architecture, memory layout, and threading model.
- [**docs/API.md**](docs/API.md) - Core API reference and complexity guarantees.
- [**docs/BENCHMARKING.md**](docs/BENCHMARKING.md) - Microbenchmark methodology.
- [**docs/AUDIT_LOGGING.md**](docs/AUDIT_LOGGING.md) - Audit trail architecture and limitations.
- [**BUILDING.md**](BUILDING.md) - Compilation instructions and sanitizer testing.
- [**SECURITY.md**](SECURITY.md) - Security policy, threat model, and vulnerability reporting.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
