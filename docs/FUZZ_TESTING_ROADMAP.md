# Parser fuzz coverage and roadmap

Baseline cdae713 reviewed 2026-09-14. This remediation adds optional ITCH, OUCH
and FIX libFuzzer entry points and a bounded CI smoke job. CI configuration is not evidence
that a hosted run has passed. No comprehensive coverage claim is made.

| Parser | Existing baseline evidence | Coverage-guided status / next step |
| --- | --- | --- |
| ITCH, luv_decode_itch.hpp | test_decoder_fuzz: fixed-seed random malformed bytes and truncated lengths | New fuzz_itch target; add valid structured seeds and coverage reports |
| OUCH, luv_ouch.hpp | test_ouch_parser: valid events plus random bytes | New fuzz_ouch target; add valid event corpus and sequence tests |
| FIX, luv_fix.hpp | test_fix_protocol | New fuzz_fix and numeric boundary regression; full session/framing coverage remains open |
| HTTP, luv_http_server.cpp | test_http_server: socket/API behavior and malformed requests | No libFuzzer target; expose bounded parsing seam, fuzz lengths/headers/auth/body |
| WebSocket, luv_websocket.cpp | test_websocket: protocol/lifecycle/integration cases | No libFuzzer target; fuzz handshake, masked frames, fragmentation and close state |
| MoldUDP64, luv_moldudp64.hpp | test_moldudp64_fixture | No libFuzzer target; header/message lengths and sequence edges |
| Recovery, luv_recovery.hpp | corruption/replay tests | No libFuzzer target; malformed records and replay transitions |

Prioritize HTTP/WebSocket (exposed by the executable), FIX numeric parsing, then
MoldUDP64 and recovery. FIX signed digit accumulation and unsigned tag wraparound
were fixed during remediation, with exact INT64_MIN/MAX and malformed-value tests.
Random smoke testing is distinct from coverage-guided fuzzing. The network tests
are more substantial than the supplied plan's example label "smoke only".

```sh
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DLUV_ENABLE_FUZZING=ON
cmake --build build-fuzz --target fuzz_itch fuzz_ouch fuzz_fix --parallel 4
./build-fuzz/fuzz_itch -runs=10000 -max_len=2048
./build-fuzz/fuzz_ouch -runs=10000 -max_len=2048
./build-fuzz/fuzz_fix -runs=10000 -max_len=2048
```

Two synthetic FIX seeds are retained in `fuzz/corpus/fix`; pass a writable copy of
that directory to fuzz_fix for a seeded campaign. CI uses these seeds. Neither
seed establishes strict FIX wire validity: pipe delimiters are accepted by the
current parser, which has additional session/framing review gaps.

Use a Clang distribution containing libFuzzer. TSan is incompatible with this
configuration. The targets enable ASan/UBSan and libFuzzer themselves. Start with
synthetic fixtures; do not commit private trading-session logs. Persist minimized
crashes as regression tests, record compiler/commit/run duration and coverage, and
retain corpus artifacts for longer CI campaigns. No historical production corpus, crash-free
campaign or coverage percentage is claimed here.
