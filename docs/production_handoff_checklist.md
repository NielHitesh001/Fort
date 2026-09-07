# Production Handoff Checklist

## Phase 1: Crash Recovery Validation

- Required: run `crash_recovery_harness` on a fresh build.
- Result: the harness must demonstrate kill -9 restart + replay without state mismatch.
- Required evidence: test output showing PASS for each crash scenario.

## Phase 2: Staging Soak Test

- Required: run `staging_soak_runner --duration 3600 --rate 100` in CI or a clean staging host.
- Required evidence: PASS output with no telemetry drift and memory growth below 10%.
- Production follow-up: run a 48–72 hour soak on a dedicated staging environment before live capital.

## Phase 3: Governance and Sign-Off

### Security & Compliance
- Regulatory audit trail review completed.
- Data protection review completed.
- Recovery ledger and telemetry reviewed for sensitive data exposure.

### Operational Safety
- Order flow validation completed.
- Risk gate enforcement reviewed and signed off.
- Failure mode drills executed for network disconnect, kill -9 restart, ACK timeout, and limit violation.

### Deployment & Operations
- Deployment runbook documented and checked by someone who did not write the code.
- Monitoring and alerting configured.
- Rollback plan rehearsed.

### Code Quality & Security
- External code review completed for execution, recovery, and telemetry paths.
- Sanitizer validation performed.
- Fuzz testing completed.

## Final Authorization

The system is authorized to progress from simulator to staging, and then to sandbox/paper trading, only after all items above are signed by the designated owners.

This checklist is a governance document; it does not replace engineering verification or operational discipline.
