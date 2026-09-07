# Production Handoff Checklist

## Status

This repository is currently validated as a research-grade / controlled-integration library. It is not a venue-certified production trading system.

Current local evidence:
- CMake Release readiness build passed
- 13 enabled tests passed; stress disabled in bounded readiness mode
- ASAN/UBSAN validation passed
- ThreadSanitizer-bounded validation passed
- Crash-recovery and audit-integrity checks passed
- Local latency sample generated on current workstation

This is strong evidence for local safety and bounded integration testing, but it does not replace external venue validation, hardware validation, or operator controls.

## Required sign-off before any real-money use

### 1) Foundation validation
- [ ] DPDK build and packet capture validated on the target host
- [ ] Hugepages, NIC bind state, and system tuning recorded
- [ ] Environments for production, staging, and test are separated
- [ ] Source of market data is validated against a known-good vendor sample

### 2) Protocol and order flow validation
- [ ] Venue-specific ITCH / OUCH / FIX spec reviewed and mapped to parser behavior
- [ ] Sample-capture regression tests pass for all production message types
- [ ] Sequence gaps, duplicates, and recoverable errors are classified and tested
- [ ] Cancel-on-disconnect and reconnect behavior is validated

### 3) Execution and reconciliation
- [ ] Order gateway authentication and permissions reviewed
- [ ] Execution reports are reconciled to local state before live use
- [ ] Risk limits and circuit breakers are configured by operators, not just defaults
- [ ] Failover and rollback procedures are documented and tested

### 4) Audit and retention
- [ ] Append-only audit trail stored outside the process memory space
- [ ] Backup and restore test passes with integrity verification
- [ ] Retention period and access policies documented
- [ ] Incident response and data preservation workflow is reviewed by a responsible authority

### 5) Operational safeguards
- [ ] Manual kill switch and safe shutdown process exists
- [ ] Alerting thresholds defined for backlog, sequence gaps, and rejects
- [ ] Escalation path for venue or network incidents is documented
- [ ] No live capital movement occurs without written operator approval

## Execution gate

A production handoff is complete only when all boxes above are executed with evidence and recorded by the operating entity.

Until then, the intended status is:
- controlled local validation complete
- external integration validation pending
- production certification not granted

## Local evidence command set

```sh
cmake -S . -B build-readiness -DCMAKE_BUILD_TYPE=Release -DLUV_ENABLE_LONG_STRESS_TESTS=OFF
cmake --build build-readiness -j2
ctest --test-dir build-readiness --output-on-failure
./build-readiness/test_latency
```

These commands are useful validation gates for the repository itself, but they are not a substitute for live-environment certification.
