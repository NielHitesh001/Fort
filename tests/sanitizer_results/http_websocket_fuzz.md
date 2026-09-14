# HTTP/WebSocket parser validation — 2026-09-14

Local Mac validation, not hosted CI or continuous fuzzing. Host: Apple M5,
arm64 macOS 26.6.2 (25G83). Fuzz and ASan/UBSan compiler: Homebrew Clang 22.1.1.
DPDK disabled. No performance claims were changed.

The fuzz executables call the same inline parsers as the control-plane listeners;
they instantiate no sockets, threads, servers, or network clients. Input and frame
storage are bounded. `hex:` corpus seeds decode into fixed byte arrays; all other
inputs are raw bytes. HTTP exercises request lines, header syntax and duplicates,
unsupported transfer encoding, decimal lengths and partial body boundaries.
WebSocket exercises upgrade headers/key validation, FIN/RSV/opcode, extended
lengths, masks, continuation state, close codes and close-reason UTF-8.

Commands (writable copies of the checked-in corpora were used locally):

```sh
cmake -S . -B build-remediation-fuzz \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DLUV_ENABLE_FUZZING=ON -DLUV_ENABLE_DPDK=OFF
cmake --build build-remediation-fuzz --target fuzz_http fuzz_websocket --parallel 4
./build-remediation-fuzz/fuzz_http -runs=50000 -max_len=4096 CORPUS_HTTP_COPY
./build-remediation-fuzz/fuzz_websocket -runs=50000 -max_len=4096 CORPUS_WS_COPY
ctest --test-dir build-remediation-asan \
  -R '^(wire_parse|http_server|websocket)$' --output-on-failure
ctest --test-dir build --output-on-failure -j4
```

Both fuzz targets completed 50,000 runs with ASan/UBSan enabled on the targets
(`-fsanitize=fuzzer,address,undefined`), with no sanitizer diagnostics or crashes.
Targeted ASan/UBSan regression: 3/3 passed, 33.05 seconds. Full Release regression:
163/163 runnable tests passed in 33.87 seconds; the existing stress test remained
disabled. Final builds emitted no compiler warnings/errors. The focused parser
tests cover malformed and truncated input, and HTTP integration tests verify
ambiguous framing and bytes beyond Content-Length never enqueue orders.

An intermediate regression run caught a changed invalid-key response message;
the established message was restored before final validation. No existing
assertions or performance thresholds were weakened.

CI is configured for 10,000-run smoke tests per new target on ubuntu-24.04.
No push or hosted CI result is claimed. Remaining parsing gaps are listed in
`docs/FUZZ_TESTING_ROADMAP.md`, notably JSON command grammar and text-message UTF-8.
