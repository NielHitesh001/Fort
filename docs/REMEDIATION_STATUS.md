# Remediation status

Baseline: cdae713; work started 2026-09-14. This is an implementation/evidence
tracker, not a readiness score or independent sign-off.

Validation: [local sanitizer/fuzz record](../tests/sanitizer_results/remediation.md).

| Plan item | Status |
| --- | --- |
| 1.1 GitHub description | Updated and read back through GitHub API. Search-engine snippet propagation unverified |
| 1.2 README scope | Existing prominent research disclaimer retained; unsubstantiated passing-test badge replaced with CI status |
| 2.1–2.2 performance | Current README/BENCHMARKING claims audited and withdrawn (Option B); source-based reproduction instructions and open WS issue documented |
| 3.1 historical files | All locally listed versions of four files reviewed; ignored against accidental re-addition; no high-risk contacts/credentials found; cache visibility unverified |
| 4.1 arithmetic | SEC 17a-5, 15c3-1 and 15c3-3 checked; explicit invalid result flags and boundary tests. Remaining compliance modules require individual review |
| 4.2 parser coverage | Coverage map, ITCH/OUCH/FIX libFuzzer targets and CI smoke job added. HTTP/WS/MoldUDP64/recovery fuzzing remains open |
| 4.3 architecture | Actual executable topology documented; research-class presence distinguished from runtime integration |
| 4.4 failure scenarios | Synthetic feed pause during partial fill and execution-slab exhaustion/WAL replay tests added. Live/replay arbitration is not implemented by the executable; deployed Raft is not in scope |
| 5.1 narratives | Current discovery template clarified; historical investor brief already removed. Private decks/emails unavailable; no messages sent |
| 5.2 external reviews | Proposed scope, evidence gates, quote/budget process documented. No review commissioned, budget approved, or dates committed |

Do not close the full remediation program on the basis of this change. Next
engineering work: audit remaining financial arithmetic, complete exposed parser
fuzzing, resolve WebSocket burst latency, and specify a live/replay ownership
protocol before claiming its recovery race is tested. External commissioning,
private-material review and future commercial/regulatory scope require owner input.

Compatibility: arithmetic result structs gain `arithmetic_valid`; callers must
check it. Invalid calculations clear approval/compliance booleans. FIX get_int
returns the provided default on overflow, sign-only or trailing-garbage input;
it now accepts exact INT64_MIN without overflow. This deliberately tightens
previous permissive numeric-prefix behavior.
