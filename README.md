# Fort: Low-Latency Market Microstructure & Order Book Simulation Framework

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.20+-green.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Tests](https://img.shields.io/badge/Tests-151%20Passing-brightgreen.svg)](tests/)

---

## ⚠️ Disclaimer: Educational & Research Software

> [!IMPORTANT]
> **This software is designed exclusively for educational, research, and algorithmic simulation purposes.**
>
> - ✗ **NOT PRODUCTION-READY FOR LIVE CAPITAL**: It does not connect to live exchange execution gateways (OUCH/FIX) or manage real financial capital.
> - ✗ **NOT A REGULATED BROKER-DEALER OR TRADING VENUE**: It does not perform live customer KYC/AML identification, FinCEN SAR filing, physical clearinghouse settlement (T+1/T+2), or custodial banking.
> - ✗ **SIMULATION ONLY**: Mathematical regulatory capital models (e.g. SEC Rule 15c3-1, 15c3-3, 17a-4/5) and quantitative pricing models are implementations for research, simulation, and academic study.
> - ✗ **AUDIT LOG IS NOT REGULATORY RECORDKEEPING**: `luv_safety.hpp` provides local hash-chain integrity checks only. It is not WORM storage and does not satisfy SEC Rule 17a-4, FINRA, or CAT retention/custody requirements.
>
> Deploying software with live financial capital requires licensed market data infrastructure, clearing memberships, certified legal/regulatory compliance programs, and independent external auditing.
>
> For full details of included vs. out-of-scope capabilities, see [**docs/CAPABILITIES.md**](docs/CAPABILITIES.md).

---

## Overview

**Fort** is a high-performance C++20 simulation framework designed for studying **market microstructure**, **order matching algorithms**, **stochastic volatility pricing**, and **low-latency systems programming techniques** (cache alignment, zero-allocation memory pools, and lock-free concurrency).

---

## Core Capabilities

- **Protocol Decoders**: Nasdaq ITCH 5.0 binary protocol parser with strict input length and bounds validation.
- **In-Memory Limit Order Book (LOB)**: Cache-aligned price-time FIFO matching engine supporting limit, market, pegged, and iceberg orders.
- **Zero-Allocation Memory Arenas**: Pre-allocated contiguous memory pools (`luv_arena.hpp`) eliminating dynamic runtime allocations on critical paths.
- **Market Microstructure Feature Extraction**: Real-time Order Flow Imbalance (OFI), micro-price estimators, and live decision tree ML inference.
- **Quantitative Derivatives Pricing**: Black-Scholes Greeks, Heston (1993) stochastic volatility, Bates (1996) jump-diffusion, and Rough Bergomi (rBergomi) fractional volatility models.
- **Multi-Region Cross-DC Consensus Simulation**: Active-active cross-datacenter state machine replication (NY4, LD4, TY3) with Hybrid Logical Clocks.
- **High-Resolution Telemetry**: Lock-free SPSC ring buffer for sub-microsecond latency measurement and percentile tracking.

---

## Quick Start

### 1. Prerequisites
- Modern C++20 compiler (Clang 13+, GCC 11+, or AppleClang)
- CMake 3.20+
- OpenSSL (Crypto library)

### 2. Build & Test

```bash
# Clone the repository
git clone https://github.com/NielHitesh001/Fort.git
cd Fort

# Configure & build release binary
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run all 151 test suites
ctest --test-dir build --output-on-failure

# Run the in-memory latency benchmark
./build/luv_latency_benchmark
```

For detailed build instructions, sanitizers (ASan/UBSan), and platform-specific options, see [**BUILDING.md**](BUILDING.md).

---

## Performance Notes

In local in-memory simulation benchmarks on modern multi-core processors:
- **LOB Order Operations**: $\approx 30 - 85\text{ ns}$ (p50 / p99)
- **ITCH 5.0 Message Parse**: $\approx 15 - 35\text{ ns}$
- **Live Tree Inference**: $\approx 35 - 75\text{ ns}$
- **Telemetry Recording**: $\approx 8 - 15\text{ ns}$

> [!NOTE]
> These figures measure **local in-memory computation only** in simulation mode. Real-world end-to-end trading latency includes network transit, optical fiber propagation, kernel socket handling, and exchange matching queueing ($\approx 50 - 500\,\mu\text{s}$). See [**docs/BENCHMARKING.md**](docs/BENCHMARKING.md) for a complete latency breakdown.

---

## Documentation Index

- [**docs/CAPABILITIES.md**](docs/CAPABILITIES.md) - Full specification of included vs. excluded features.
- [**docs/ARCHITECTURE.md**](docs/ARCHITECTURE.md) - System architecture, memory layout, and threading model.
- [**docs/API.md**](docs/API.md) - Core API reference, class hierarchy, and complexity guarantees.
- [**docs/EXAMPLES.md**](docs/EXAMPLES.md) - Compilable code examples and tutorial walkthroughs.
- [**docs/BENCHMARKING.md**](docs/BENCHMARKING.md) - Microbenchmark methodology and latency analysis.
- [**docs/AUDIT_LOGGING.md**](docs/AUDIT_LOGGING.md) - Audit trail architecture and compliance standards.
- [**docs/COMPLIANCE_STATUS.md**](docs/COMPLIANCE_STATUS.md) - Simulation scope and regulatory-status matrix.
- [**docs/TROUBLESHOOTING.md**](docs/TROUBLESHOOTING.md) - Structured failure events and operator responses.
- [**BUILDING.md**](BUILDING.md) - Compilation instructions and sanitizer testing.
- [**SECURITY.md**](SECURITY.md) - Security policy, threat model, and memory safety guarantees.
- [**ROADMAP.md**](ROADMAP.md) - Future research and architectural roadmap.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
