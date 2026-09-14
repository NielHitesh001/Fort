# Performance claim audit

Reviewed 2026-09-14 against baseline `cdae713`. This is a source/provenance audit,
not a new performance measurement.

| Claim at baseline | Source | Evidence assessment / action |
| --- | --- | --- |
| Engine p50 8 microseconds | GitHub About metadata | No matched reproducible artifact found; removed from metadata |
| LOB 30–85 ns; ITCH 15–35 ns; tree 35–75 ns; telemetry 8–15 ns | README Performance Notes | No host/commit/run identified; withdrawn |
| Insertion 45/85 ns; cancellation 30/60 ns; ITCH 15/35 ns; OFI 10/25 ns; tree 35/75 ns; telemetry 8/15 ns | BENCHMARKING section 2 | No matched raw output or host configuration; withdrawn |
| Feed 20–150 us; NIC 1–3 us; parsing .03 us; LOB .05 us; strategy .05 us; risk .03 us; gateway 1–3 us; network 20–200 us; total 45–400 us | BENCHMARKING section 3 | Unsourced illustrative estimates, not measurements; removed |
| Network 50–500 us; transatlantic 35 ms; local propagation 10–500 us | README / BENCHMARKING | Unsourced contextual estimates; removed |
| WebSocket burst approximately 308 us P99 | SEPT_11_STATUS and commit 6f1d95b message | Historical report, incomplete provenance; retained only as issue history |
| WebSocket burst approximately 300–365 us P99 | Local prior session on macOS, preceding cdae713 | Failed assertion observed, incomplete host metadata; not a reproducible published benchmark |

`test_latency.cpp` is an available measurement program, not evidence for every
number formerly in the README. `test_websocket.cpp` separately measures callback
to outbound-buffer queueing, not delivery over the network. The 100 us assertions
are targets, not achieved guarantees. No production SLA is established here.

Scope: current public description, README and BENCHMARKING, plus relevant sprint
history. Historical evidence files elsewhere retain their original dates and are
not current performance claims. Their numbers require independent provenance review
before reuse. This audit does not certify every numeric statement in the repository.
