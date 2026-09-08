# Building Fort

This guide explains how to configure, compile, and run Fort across different environments and build configurations.

---

## 1. Prerequisites

- **C++ Compiler**: Modern C++20 compliant compiler:
  - Clang 13+ (or AppleClang 13+)
  - GCC 11+
- **Build System**: CMake 3.20+ and Ninja / Make
- **Libraries**:
  - POSIX Threads (`Threads::Threads`)
  - OpenSSL Crypto (`OpenSSL::Crypto`)
  - Optional: Linux DPDK (for kernel bypass networking on Linux)

---

## 2. Standard Release Build

```bash
# Configure release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets with multi-core parallelism
cmake --build build --parallel

# Execute all test suites
ctest --test-dir build --output-on-failure
```

---

## 3. AddressSanitizer & UBSan Build

To compile with AddressSanitizer and UndefinedBehaviorSanitizer enabled:

```bash
cmake -S . -B build-asan -DLUV_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

## 4. Running Benchmarks & Simulations

```bash
# In-Memory Latency Benchmark
./build/luv_latency_benchmark

# Staging Simulation Runner
./build/luv_staging_runner
```
