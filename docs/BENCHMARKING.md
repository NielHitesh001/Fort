# Fort Latency & Performance Benchmarking Analysis

This document provides transparent, reproducible benchmarking methodology and analysis for the Fort low-latency simulation engine.

---

## 1. Scope of Benchmarking

The benchmark results reported by Fort measure **local in-memory processing latency** under controlled simulation conditions.

### What is Included:
- In-memory Limit Order Book (LOB) price-level lookups and modifications.
- Zero-allocation memory pool operations (`luv_arena.hpp`).
- Feature extraction calculations (OFI, microprice, book imbalance).
- Lock-free telemetry ring buffer sample recording.

### What is Excluded:
- Network socket I/O and kernel network stack transit.
- Optical fiber physical propagation delay (e.g. transatlantic ~35 ms, cross-subway ~10-500 $\mu\text{s}$).
- External exchange matching engine queueing delays.
- Hardware NIC interrupt handling and PCIe bus contention.

---

## 2. In-Memory Microbenchmark Breakdown

Tested on modern multi-core x86_64 / Apple Silicon architectures with compiler optimization `-O3`:

| Component | Metric | Typical Latency | Complexity |
| :--- | :---: | :---: | :---: |
| **LOB Order Insertion** | p50 / p99 | $45\text{ ns} / 85\text{ ns}$ | $O(\log N)$ |
| **LOB Order Cancellation** | p50 / p99 | $30\text{ ns} / 60\text{ ns}$ | $O(1)$ direct / $O(\log N)$ |
| **ITCH 5.0 Message Parse** | p50 / p99 | $15\text{ ns} / 35\text{ ns}$ | $O(1)$ |
| **OFI & Micro-Price Update**| p50 / p99 | $10\text{ ns} / 25\text{ ns}$ | $O(1)$ |
| **Live Tree ML Inference** | p50 / p99 | $35\text{ ns} / 75\text{ ns}$ | $O(D \cdot T)$ |
| **Lock-Free Telemetry Write**| p50 / p99 | $8\text{ ns} / 15\text{ ns}$ | $O(1)$ atomic increment |

---

## 3. Realistic End-to-End Latency vs. Local Simulation

To understand live market performance, the local in-memory execution must be combined with physical network latencies:

```
+-------------------------------------------------------------------------+
|                  Realistic End-to-End Trading Cycle                     |
+-------------------------------------------------------------------------+
|  1. Exchange Multicast Feed Arrival (Network):     ~ 20 - 150 us        |
|  2. NIC Kernel Bypass (DPDK/Solarflare):           ~ 1 - 3 us           |
|  3. ITCH Protocol Parsing (Fort Decoder):          ~ 0.03 us (30 ns)    |
|  4. LOB Matching & Microstructure Feature Update:  ~ 0.05 us (50 ns)    |
|  5. Strategy Signal / Pricing Computation:         ~ 0.05 us (50 ns)    |
|  6. Pre-Trade Risk Validation:                     ~ 0.03 us (30 ns)    |
|  7. Order Gateway Transmission (OUCH/FIX):         ~ 1 - 3 us           |
|  8. Physical Network Transit to Exchange Matching: ~ 20 - 200 us        |
+-------------------------------------------------------------------------+
|  Total End-to-End Cycle:                           ~ 45 - 400 us        |
+-------------------------------------------------------------------------+
```

*Conclusion*: Real-world end-to-end latency is dominated by network transmission and exchange queueing, while Fort's internal core processing is kept within tens of nanoseconds.

---

## 4. Running the Benchmark

You can run the microbenchmark suite locally:

```bash
# Build release configuration
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Execute latency benchmark
./build/luv_latency_benchmark
```
