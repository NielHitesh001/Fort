# Security & Safety Policy

## 1. Security Design Principles

Fort is an educational and research framework for market microstructure simulation and C++ latency optimization. It incorporates several core software security and memory safety principles:

- **Strict Pre-Allocation**: Dynamic allocations (`malloc`/`new`) are prohibited on critical processing paths, preventing memory exhaustion attacks and fragmentation vulnerabilities.
- **Input Validation & Sanitization**: Incoming binary ITCH packets are validated for minimum buffer lengths, message type correctness, and numeric range bounds before parsing.
- **Continuous Sanitizer Verification**: All 151 test suites are continuously tested under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) to guarantee zero memory corruption, buffer overflows, or undefined behavior.
- **Cryptographic Record Verification**: Audit logs utilize cryptographic previous-block hash chaining to detect any tampering or record deletion.

---

## 2. Limitations & Exclusions

Because Fort is designed for simulation and educational research:
- It does **not** include network-level DDoS mitigation or transport-layer TLS encryption.
- It does **not** manage external user credentials, API keys, or financial custody private keys.
- It is **not** audited or certified for live financial capital management.

---

## 3. Testing with LLVM Sanitizers

To build and verify the codebase with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-asan -DLUV_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

## 4. Reporting Vulnerabilities

If you discover a security vulnerability or bug within this educational framework, please open an issue on GitHub or submit a pull request with regression tests.
