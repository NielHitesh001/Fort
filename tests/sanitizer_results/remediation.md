# Remediation validation record

2026-09-14; local macOS; baseline cdae713 plus this change. Clang 22.1.1
(`/opt/homebrew/opt/llvm/bin/clang++`), OpenSSL 3.6.4, DPDK disabled.

Normal CTest: all 6 selected arithmetic/FIX tests passed, followed by both new
failure tests. The initial exhaustion test expected automatic admission recovery;
it correctly failed because capacity exhaustion latches a symbol halt. The final
test verifies that halt and explicit owner-thread resume before slot reuse.

ASan/UBSan configuration:

```sh
cmake -S . -B build-remediation-asan -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DCMAKE_BUILD_TYPE=Debug -DLUV_ENABLE_ASAN_UBSAN=ON
cmake --build build-remediation-asan --target luv_sec_rule_17a5 luv_compliance_arithmetic_boundaries luv_fix_numeric_boundaries luv_feed_disconnect_partial_fill luv_arena_exhaustion_recovery --parallel 4
ctest --test-dir build-remediation-asan -R '^(sec_rule_17a5|compliance_arithmetic_boundaries|fix_numeric_boundaries|feed_disconnect_partial_fill|arena_exhaustion_recovery)$' --output-on-failure
```

Result: 5/5 passed; exit 0; no sanitizer diagnostics. This does not claim macOS
LeakSanitizer support or a full-repository sanitizer pass.

ITCH, OUCH and FIX libFuzzer targets each completed 10,000 local iterations with
ASan/UBSan enabled, exit 0. Initial seeds: ITCH 2822935337, OUCH 2823445500,
FIX 2931160105. A second FIX run used the checked-in synthetic seeds copied to a
temporary writable corpus and `-seed=1`; 10,000 runs completed without diagnostics.
These short smoke campaigns provide limited coverage, not proof of parser safety.

The standalone SEC arithmetic builds also passed with `-Wsign-conversion` and
UBSan. The optional repository-wide warning audit has not been completed.

Full regression, TSan, HTTP/WS fuzzing, independent external review and the
WebSocket performance gate are not signed off by this record.
