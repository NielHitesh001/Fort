# Validation Sprint Log

Repository: `NielHitesh001/Arbor`
Engine: `Arbor / LUV Flicker`
Date: 2026-09-05

## Verified Local Gates

- [x] CMake Debug configure/build
- [x] Release CMake/CTest with long stress disabled: 13 enabled tests passed; stress intentionally skipped in bounded readiness mode
- [x] ASAN/UBSAN CMake build and CTest: 14/14 passed
- [x] Bounded ThreadSanitizer CMake build and CTest: 13 enabled tests passed; stress intentionally skipped in bounded sanitizer profile
- [x] Audit hash-chain corruption rejection
- [x] OUCH parser fuzz smoke: 100,000 inputs
- [x] Risk validator fuzz smoke: 50,000 inputs
- [x] Process-level SIGKILL recovery replay
- [x] Release `test_latency` target builds and runs on the current checkout
- [x] `git diff --check`
- [ ] Target-hardware DPDK validation
- [ ] 24-hour stability and production-scale traffic
- [ ] Venue-specific protocol certification

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan -DLUV_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan -j2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan-bounded -DLUV_ENABLE_TSAN=ON \
  -DLUV_ENABLE_LONG_STRESS_TESTS=OFF
cmake --build build-tsan-bounded -j2
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan-bounded --output-on-failure
```

## Local Latency Sample

Run `test_latency` on the current checkout. These numbers are local simulation
measurements and are not an exchange or production SLA.

```text
Compiler: AppleClang / C++20 Release build
Host: macOS local development workstation
Build flags: -O2, Release, asserts preserved
 p50 ns: 1667
 p99 ns: 9000
 p999 ns: 36375
 Samples: 20000
 Result: observed local simulation baseline; not an SLA
```

## Remaining Evidence

Production readiness still requires target-hardware DPDK tests, venue-specific
transport/certification, authenticated execution reports, external audit
retention, failover/cancel-on-disconnect, and long-duration stability data.

## Validation Finding

The first CMake Release benchmark crashed because the benchmark target allowed
setup assertions to compile out. The target now preserves `assert` checks like
the regression tests; the rebuilt Release benchmark passes. This is why
benchmark setup checks remain enabled in test targets.
