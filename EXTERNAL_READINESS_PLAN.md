# External Readiness Plan

This repo is currently validated at the local library and sanitizer level, but it is not a venue-certified production system. The next work must be done against a real exchange environment and operated under explicit controls.

## Objective

Move from local evidence to external validation for real market-data and order-routing integration while keeping risk bounded and claims transparent.

## Phase 1 — Venue and hardware validation

### Gate 1: DPDK integration on target hardware

Required evidence:
- DPDK build on the target host with version and NIC compatibility documented
- Hugepages configured and reserved for the runtime user
- NIC bind/driver state recorded before the feed is enabled
- Packet capture and sequence continuity checks against test traffic
- Crash/restart replay validation on the identical target hardware path

Minimum acceptance:
- Feed captures known-good market-data samples without packet loss under operating load
- Sequence gaps are detected and classified as either recoverable or fatal
- Feed source can be restarted without data corruption or silent state drift

### Gate 2: Venue-specific protocol certification

Required evidence:
- Broker or exchange packet spec mapped to the parser and order adapter
- Message schema validation against a real sample capture
- Field-level acceptance tests for all control and trading messages used in production
- Independent review of any vendor-specific behavior, timestamps, and order IDs

Minimum acceptance:
- Parser behavior matches the venue’s specification exactly
- Every order and execution path is reconciled to a real reference dataset
- Non-standard or malformed packets are rejected with explicit diagnostics

## Phase 2 — Execution and reconciliation controls

### Gate 3: Order gateway validation

Required evidence:
- Authenticated execution sessions and operator-controlled endpoints
- Cancel-on-disconnect, failover, and re-subscription behavior tested
- Execution reports confirmed against locally reconstructed state
- Risk and position checks tied to policy enforcement, not just local bookkeeping

Minimum acceptance:
- A forced disconnect does not leave the engine in a silent state
- Reconnect logic is deterministic and audit-able
- Rejects, cancels, and fills are reconciled before any live entitlement is used

### Gate 4: Audit retention and backup

Required evidence:
- Append-only audit log stored outside the live process namespace
- External backup and restore tests with hash-chain or signed record integrity checks
- Retention and access controls documented for operators and auditors
- Recovery procedures reviewed by someone independent of the development team

Minimum acceptance:
- Pipeline can replay the audit log after a full restore
- Corruption is detected before state restoration continues
- Audit retention supports the compliance time horizon required by the operating entity

## Phase 3 — Stability and operational runbook

### Gate 5: Long-duration soak and failover tests

Required evidence:
- 24-hour or equivalent stability test on representative hardware
- Simulated failover and restart loops with recovered state checks
- Resource leak checks and long-run memory/CPU observations
- Latency bucket reporting at p50, p99, and p999 under sustained load

Minimum acceptance:
- No unbounded growth in memory, order book state, or ring backlog
- Recovery and restart remain deterministic under repeated faults
- Alerting thresholds are defined and operable before live trading

### Gate 6: Controlled operator readiness

Required evidence:
- Written runbook with startup, shutdown, rollback, and incident procedures
- Authorized staff and escalation contacts defined
- Clear manual kill switch and safe default stance for any unknown feed or execution condition
- Review of legal, compliance, and capital-risk responsibilities before any live use

Minimum acceptance:
- Operators can perform a safe shutdown without ambiguous state
- Every high-risk condition is mapped to a documented response
- No real-money operation proceeds without sign-off from the operating authority

## Recommended command and evidence set

```sh
cmake -S . -B build-readiness -DCMAKE_BUILD_TYPE=Release -DLUV_ENABLE_LONG_STRESS_TESTS=OFF
cmake --build build-readiness -j2
ctest --test-dir build-readiness --output-on-failure
./build-readiness/test_latency
```

This local evidence confirms the library remains stable and bounded, but it does not substitute for target-hardware, venue-specific, or operational validation.

## Final gate

The project should only be treated as production-ready for a specific venue after all external gates above are satisfied by independent validation, not by the repository authors alone.
