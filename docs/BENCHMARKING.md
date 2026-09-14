# Benchmark methodology

The earlier numeric tables are withdrawn: no retained run linked them to a host,
compiler configuration, and commit. See [claim audit](PERFORMANCE_AUDIT_FINDINGS.md).

`test_latency.cpp` builds as `luv_latency_benchmark`. It exercises local synthetic
operations; it does not establish live venue execution, NIC-backed DPDK performance,
audit persistence latency, or an order-to-ack SLA. Timing overhead and compiler
optimization affect these short measurements. Inspect the source's timed regions
before attributing results to a complete engine pipeline.

```sh
git rev-parse HEAD
git diff --stat
uname -a
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLUV_ENABLE_DPDK=OFF
cmake --build build --target luv_latency_benchmark luv_websocket --parallel 4
./build/luv_latency_benchmark
./build/test_websocket --bench
```

Retain UTC date, CPU model/core count, RAM, kernel, compiler, CMake cache, commit,
uncommitted patch, raw output and exit status with every published result. Record
CPU contention, sample count, warmup and the precise measured boundaries. On macOS
use `sysctl -n machdep.cpu.brand_string hw.ncpu hw.memsize`; on Linux use `lscpu`
and `/proc/meminfo`. Report unavailable details explicitly.

The WebSocket suite currently has a failing burst-latency assertion. A command
aborting before its final table is a failed run, not a complete benchmark report.
See [known issues](KNOWN_ISSUES.md#websocket-burst-latency).
