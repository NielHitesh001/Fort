# Gate A Closure Evidence

Date: 2026-09-07
Repository: `NielHitesh001/Arbor`
Engine: `Arbor / LUV Flicker`
Scope: Local engineering validation only

## Executive Status

**Gate A: NOT PASSED.** The local test evidence below is reproducible and the
worktree is currently clean, but the external staging prerequisites and remote
push have not been verified from this workspace.

This record does not authorize staging or live-capital deployment.

## Repository State

Command:

```text
git status --short --branch
```

Result: `main...origin/main [ahead 1]`; the worktree is clean. The local branch
contains one commit not present on `origin/main`.

HEAD:

```text
f51c96805027267682683ea7812988514f62cb31
```

HEAD commit date: `2026-09-07 10:06:21 +0530`

HEAD subject: `hii`

`git diff --check`: **PASS**.

## Build Artifacts

The following executable artifacts were present and identified as arm64
Mach-O binaries on macOS:

- `build-day4/crash_recovery_harness`
- `build-phase2/staging_soak_runner`

Metadata recorded locally:

| Binary | Size | Filesystem timestamp | Format |
| --- | ---: | --- | --- |
| `build-day4/crash_recovery_harness` | 89,704 bytes | 2026-09-07 08:22:14 +0530 | arm64 Mach-O executable |
| `build-phase2/staging_soak_runner` | 36,384 bytes | 2026-09-07 07:47:51 +0530 | arm64 Mach-O executable |

The `build-day4` tree was rebuilt successfully with CMake. Both required
targets linked successfully.

## Crash Recovery

Command:

```text
./build-day4/crash_recovery_harness
```

Observed at approximately `2026-09-07` local execution time:

```text
Crash after order 10: crashed=YES replay_events=10 active=10 net_position=1450
SCENARIO Crash after order 10: PASS
Crash after order 25: crashed=YES replay_events=25 active=25 net_position=3550
SCENARIO Crash after order 25: PASS
Crash after order 50: crashed=YES replay_events=50 active=50 net_position=7250
SCENARIO Crash after order 50: PASS
```

Result: **3/3 scenarios passed**. The requested 5/5 result is not supported by
the current harness; it defines three scenarios.

The full `build-day4` CTest run also passed **16/16 tests** in 31.52 seconds,
including `crash_recovery_harness` and `test_crash_recovery_process`.

## Local Soak Test

Command:

```text
./build-phase2/staging_soak_runner --duration 60 --rate 100 \
  --ledger /tmp/luv_phase_a_soak_ledger.bin
```

Execution window: `2026-09-07T02:53:02Z` to `2026-09-07T02:54:02Z`.

```text
Starting soak test: 0.0 hours at 100 orders/sec

Soak complete. Final state:
  Orders sent: 5900
  Orders approved: 64
  Orders rejected: 5836
  Orders filled: 64
  Memory check: RSS 889536512 -> 889536512 (delta=0, 0.0%)
  Telemetry drift check: PASS
  Reconciliation: PASS
  Soak test result: PASS
```

Result: **PASS for the runner's local checks**. This is not a 48-72 hour
staging soak, does not establish p99 latency, and does not demonstrate a 100%
fill rate. The local gateway rejected 5,836 orders during this run.

## ASAN/UBSAN

Configuration from `build-asan/CMakeCache.txt`:

```text
CMAKE_BUILD_TYPE:STRING=Debug
LUV_ENABLE_ASAN_UBSAN:BOOL=ON
LUV_ENABLE_LONG_STRESS_TESTS:BOOL=ON
LUV_ENABLE_TSAN:BOOL=OFF
```

The configured ASAN/UBSAN CTest run passed **14/14 tests** in 91.54 seconds.
No sanitizer failure or leak report was emitted. This is local test evidence,
not proof of sanitizer cleanliness for the unexecuted staging workload.

## External Prerequisites

The following items were not verifiable from this local workspace and remain
open:

- AWS `t3.medium` instance running
- OUCH replay feed accessibility
- Prometheus and Grafana configuration/live dashboards
- Evidence push to the remote repository
- Engineering/QA approval of the gate

## Gate Decision

**NO-GO / PENDING.** Local binaries and validation runs are available, and the
worktree is clean. Gate A cannot close until this evidence commit is pushed to
the agreed remote location, staging prerequisites are confirmed, and the
Engineering Lead/QA sign-off is recorded.
