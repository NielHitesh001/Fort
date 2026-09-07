# LUV---Flicker Project Report

**Date:** 2026-09-08  
**Repository:** `NielHitesh001/LUV---Flicker-`  
**Current branch:** `main`  
**Current release commit:** `f3c57c3` plus subsequent hardening commits in the local history

## 1. Executive Summary

LUV---Flicker is a C++20 market-microstructure and execution-control prototype. It
processes ITCH-style market data, reconstructs order-book state, evaluates
pre-trade risk, builds OUCH-style order packets, records audit/recovery data,
and exposes telemetry and validation tools.

The project is currently best described as **validated for controlled local and
synthetic pilot work**. It is not evidence of live exchange readiness, broker
connectivity, regulatory approval, customer traction, or a production SLA.

## 2. Main Components

- `luv_arena.hpp`: preallocated aligned memory and ring structures.
- `luv_feed_sim.hpp`: deterministic synthetic and replay feed source.
- `luv_feed_dpdk.hpp`: Linux DPDK feed adapter behind the packet-I/O boundary.
- `luv_decode_itch.hpp`: bounded ITCH-style message decoding.
- `luv_lob.hpp`: limit-order-book state and mutation logic.
- `luv_consumer.hpp`: feed-to-strategy consumption path.
- `luv_execution.hpp`: risk checks, rate limiting, circuit breaker, order
  admission, OUCH packet construction, and execution-report handling.
- `luv_safety.hpp`: audit hash chain, sequence tracking, shutdown controller,
  reconciliation, and rate limiting.
- `luv_recovery.hpp`: durable recovery ledger and replay.
- `luv_telemetry.hpp`: telemetry batching, metrics, and health checks.
- `packet_io.h`, `packet_io_stub.c`, `packet_io_dpdk.c`: portable packet-I/O
  interface with macOS/CI stub and optional Linux DPDK backend.
- `main_engine.cpp`: simulation engine with graceful shutdown and optional UDP
  egress.
- `staging_runner.cpp`: synthetic paper-trading validation runner.

## 3. Prerequisites

### macOS development

- macOS
- CMake 3.20 or newer
- C++20 compiler, normally AppleClang
- OpenSSL development package discoverable by CMake
- POSIX threads, provided by the platform

Homebrew setup:

```bash
brew install cmake openssl
```

### Linux DPDK validation

- Linux host or container
- CMake 3.20 or newer
- `libdpdk-dev`
- `libssl-dev`
- `pkg-config`
- A usable DPDK-capable NIC is required for hardware-backed validation.
  Container validation may build and exercise fallback/emulated paths but is not
  equivalent to a NIC-backed venue run.

The repository includes [Dockerfile.dpdk](Dockerfile.dpdk) for the Linux build
environment.

## 4. Standard macOS Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the complete default test suite:

```bash
ctest --test-dir build --output-on-failure
```

The expected result for the current validated state is 22 passing tests. Test
counts can change when CMake options change, so always retain the exact command
and commit with any evidence.

## 5. Run the Simulation Engine

Basic bounded run:

```bash
./build/luv_engine --messages 100000
```

Optional UDP egress to localhost:

```bash
./build/luv_engine --messages 100000 --udp-port 9000
```

Optional trusted model loading:

```bash
./build/luv_engine --messages 100000 --model /absolute/path/to/model.so
```

Usage:

```text
./build/luv_engine [--messages N] [--model PATH] [--udp-port PORT]
```

The engine uses a synthetic feed unless a model is supplied. With no model,
orders are generally not accepted because no strategy signal is produced. UDP
send failures cause a nonzero process result after the outbound queue drains.
SIGINT and SIGTERM request graceful shutdown.

## 6. Synthetic Paper-Trading Validation

Build the staging runner:

```bash
cmake --build build --target luv_staging_runner --parallel
```

Run a small smoke test:

```bash
./build/luv_staging_runner \
  --orders 150 \
  --rate-hz 10 \
  --ledger /tmp/luv_staging_150.bin
```

Controls:

- `--orders N`: number of synthetic orders; must be nonzero.
- `--rate-hz N`: pacing rate; `0` disables pacing.
- `--ledger PATH`: recovery-ledger output path.

The runner checks risk decisions, simulated fills, telemetry accounting, and
ledger replay. It does not connect to a broker or exchange. The recorded
bounded result is in [PAPER_TRADING_VALIDATION.md](PAPER_TRADING_VALIDATION.md).

A 48-hour-equivalent synthetic run at 10 orders per second is:

```bash
./build/luv_staging_runner \
  --orders 1728000 \
  --rate-hz 10 \
  --ledger /tmp/luv_staging_48h_$(date +%Y%m%d).bin
```

This command is a workload recipe, not evidence that the run has completed.

## 7. Benchmarks

Build and run the local benchmark:

```bash
cmake --build build --target luv_latency_benchmark --parallel
./build/luv_latency_benchmark
```

Record compiler, operating system, architecture, build type, commit, feed
configuration, and raw output. The benchmark measures local simulated LOB/risk
operations; it does not establish a feed-to-execution or production network
SLA.

## 8. Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLUV_ENABLE_ASAN_UBSAN=ON \
  -DLUV_ENABLE_LONG_STRESS_TESTS=OFF
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

ThreadSanitizer configuration:

```bash
cmake -S . -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLUV_ENABLE_TSAN=ON \
  -DLUV_ENABLE_LONG_STRESS_TESTS=OFF
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

Sanitizer results are environment- and configuration-specific. Retain raw CI
artifacts before making a broad claim about the codebase.

## 9. Linux DPDK Build

Build the supplied validation image:

```bash
docker build -t luv-dpdk -f Dockerfile.dpdk .
```

Build and run focused Linux tests:

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  luv-dpdk bash -lc \
  "cmake -S . -B build-dpdk -DCMAKE_BUILD_TYPE=Release -DLUV_ENABLE_DPDK=ON && \
   cmake --build build-dpdk --parallel 4 && \
   ctest --test-dir build-dpdk --output-on-failure -R 'feed|execution'"
```

`LUV_ENABLE_DPDK=ON` is intentionally Linux-only. The macOS build uses the
portable stub backend. Hardware-backed DPDK validation requires a real Linux
host with the intended NIC, hugepages, queues, and permissions.

## 10. CMake Options

- `LUV_ENABLE_ASAN_UBSAN=ON`: enable AddressSanitizer and UBSan.
- `LUV_ENABLE_TSAN=ON`: enable ThreadSanitizer.
- `LUV_ENABLE_LONG_STRESS_TESTS=ON`: enable the long stress test in CTest.
- `LUV_STRESS_MESSAGES=N`: configure stress-message count.
- `LUV_ENABLE_DYNAMIC_MODEL_LOADING=ON`: enable trusted dynamic model loading.
- `LUV_REQUIRE_MLOCK=ON`: fail arena initialization if memory cannot be locked.
- `LUV_ENABLE_DPDK=ON`: enable the Linux DPDK packet-I/O backend.

## 11. Evidence and Limitations

Current repository evidence includes local CTest runs, a bounded synthetic
paper-trading result, packet-I/O tests, Linux container DPDK tests, and focused
recovery/risk regressions. These prove selected software behavior under stated
conditions.

They do not prove:

- live venue or broker integration;
- hardware-backed DPDK performance;
- customer adoption, revenue, or signed pilots;
- regulatory classification or legal compliance;
- distributed failover or multi-node operation;
- production latency or uptime SLAs.

See [INVESTOR_READINESS_BRIEF.md](INVESTOR_READINESS_BRIEF.md) for the
investor-facing scope and diligence checklist.

## 12. Clean Build Artifacts

Build directories are disposable and should not be committed:

```bash
rm -rf build build-asan build-tsan build-dpdk
```

Check repository state:

```bash
git status --short --branch
```

A clean checkout reports only the branch line, for example:

```text
## main...origin/main
```
