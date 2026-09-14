# WebSocket measurements — local simulation

Source commit: `9e5f353a9870cc96ff85aefa8cadf1772a592ce1`.
Captured 2026-09-14, approximately 16:40 UTC (22:10 IST).
Host: Apple M5, 10 logical CPUs, 16 GiB RAM; memory type/clock not recorded.
OS: macOS 26.6.2, Darwin 25.6.0, arm64. Compiler: Apple Clang 21.0.0
(clang-2100.3.34.2). Release CMake configuration, `-O3`, DPDK disabled,
OpenSSL Crypto 3.6.4. No CPU affinity or exclusive host reservation.

These are local testing only: synthetic gateway fills and loopback/socketpair
clients. The measured interval starts at WebSocket publish and ends when JSON is
queued into subscriber buffers. It is not order-to-ack, network delivery latency,
or a live venue/DPDK measurement. There is no persistence/compliance processing in
the timed callback-to-queue boundary.

| Scenario | Samples | P50 | P99 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| One subscriber | 128 | 3 us | 5 us | 6 us |
| Ten subscribers | 128 | 4 us | 6 us | 6 us |
| One hundred subscribers | 512 | 7 us | 11 us | 21 us |
| 100 disjoint fills, 1,000 active connections | 100 | 45 us | 75 us | 75 us |

The burst run received all fills and reported aggregate throughput 154,798 fills/s
over this short scenario; this is not a sustained throughput guarantee. Default
keepalive arrived after 30.001 seconds. Forced kernel EAGAIN closed after 5.585 ms.
The separate application-buffer exhaustion test recorded immediate closure (0 ns
in that diagnostic); it must not be interpreted as a measured kernel timeout.

## Reproduce

```sh
git checkout 9e5f353a9870cc96ff85aefa8cadf1772a592ce1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLUV_ENABLE_DPDK=OFF
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure -j 4
./build/test_websocket --bench
```

Sources: `test_websocket.cpp`, `luv_websocket.cpp`; output is retained in
`tests/sanitizer_results/websocket_release.txt` in the evidence commit following
the source commit. CTest runs this suite serially relative to other tests. OS
scheduling and other applications can still change results. No threshold was relaxed.

The full release run passed 162 tests; one long stress test was disabled. Earlier
unbounded-send revisions in this session failed timing gates, including a burst
P99 of 246 us. Limiting fresh sends to 16 per turn, checking pending fills between
sends, and bounding readiness processing addressed that observed delay. Those
failed intermediate runs are not successful evidence for this commit.

Final HTTP/WebSocket tests also passed under separate ASan/UBSan and TSan builds,
without diagnostics. Commands and outputs are retained in
`tests/sanitizer_results/websocket_validation.txt`. Sanitizer timing is not used
for the release measurements above.

Linux uses the existing poll fallback. Docker's daemon was unavailable locally,
so neither Linux runtime behavior nor hardware DPDK was validated by these runs.
The new macOS CI job is configured, but a hosted run is not claimed here. Meeting
these local measurements does not establish production readiness or a universal SLA.
