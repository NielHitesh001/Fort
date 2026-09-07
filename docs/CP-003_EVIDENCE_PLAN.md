# CP-003 Evidence-Backed Deterministic Execution Core

Status: **in progress**  
Repository: `NielHitesh001/Arbor`  
Engine: `Arbor / LUV Flicker`  
Evidence date: `2026-09-07`

## Purpose

CP-003 converts engineering claims into traceable records:

```text
requirement -> invariant -> test -> execution -> artifact -> verdict
```

This contract separates deterministic software evidence from deployment and
venue evidence. A passing local test cannot close a hardware, exchange,
security, or operational requirement.

## Evidence Record

Each recorded result should provide these fields:

```text
evidence_id       stable identifier, for example EVD-LOB-001
requirement_id    requirement being evaluated
invariant         falsifiable statement under test
test              executable test or command
commit            source revision under test
environment       OS, compiler, build type, and relevant hardware
inputs            fixture, seed, message count, or capture identifier
result            PASS, FAIL, BLOCKED, or NOT_RUN
artifact          path to raw output or report
artifact_sha256   digest of the artifact when retained
limitations       claims this result does not support
recorded_at       UTC timestamp
```

Missing fields make a result incomplete rather than implicitly passing.

The first captured record is
`docs/CP-003_EVIDENCE_2026-09-07.json`. Its raw CTest log is retained outside
the source tree at `/tmp/arbor-cp003-evidence.log` and is identified by the
SHA-256 digest recorded in the JSON file.

## Initial Traceability Matrix

| Requirement | Invariant | Existing check | Evidence status |
| --- | --- | --- | --- |
| REQ-LOB-001 | Invalid order locations never dereference LOB state. | `test_lob` | Local test evidence exists; record to be captured. |
| REQ-FEED-001 | Malformed ITCH lengths and semantic fields are rejected. | `test_feed`, `test_decoder_fuzz` | Local smoke evidence exists; protocol coverage remains open. |
| REQ-FEED-002 | Sequence gaps, duplicates, and out-of-order values are classified deterministically. | `test_sequence_tracker` | Local primitive evidence exists; packet recovery remains open. |
| REQ-FEED-003 | MoldUDP64 headers and declared length-prefixed message counts reject truncated, mismatched, or trailing input deterministically. | `test_moldudp64_fixture` | Synthetic wire-format evidence exists; NIC and venue behavior remain open. |
| REQ-CONC-001 | LOB mutation and shared reads obey the documented ownership boundary. | `test_concurrency` | Focused evidence exists; complete application race freedom remains open. |
| REQ-RISK-001 | Admission rejects invalid orders and enforces bounded active capacity. | `test_execution`, `test_risk_validator` | Local evidence exists; venue controls remain open. |
| REQ-AUDIT-001 | Corrupt or truncated recovery records are rejected deterministically. | `test_audit_integrity`, `test_crash_recovery` | Local evidence exists; external retention remains open. |
| REQ-PERF-001 | The simulated execution path reports reproducible percentile latency. | `test_latency` | Local baseline only; target-hardware SLA remains open. |

## Verdict Rules

- **VERIFIED-LOCAL**: the invariant passed with a complete, reproducible local
  evidence record.
- **OPEN-INTEGRATION**: local evidence exists, but an external boundary such
  as venue transport, hardware, or process recovery is untested.
- **BLOCKED**: the required environment, fixture, or authority is unavailable.
- **NOT-APPLICABLE**: the requirement is outside this repository's scope and
  has an explicit owner elsewhere.

Only **VERIFIED-LOCAL** may be used for a software-invariant claim. No CP-003
verdict authorizes production deployment or real-money trading.

## Next Closure Actions

1. Capture one machine-readable record for each remaining local test result;
  the bounded Release/CTest baseline is recorded above.
2. Store raw outputs outside the source tree and record their SHA-256 digests.
3. Add protocol fixtures for MoldUDP64 gaps, duplicates, and reordering.
4. Run the latency and soak profiles on declared target hardware.
5. Add authenticated execution-report fixtures before claiming reconciliation.

The remaining venue, operational, regulatory, and disaster-recovery controls
require independent owners and cannot be closed by unit tests alone.