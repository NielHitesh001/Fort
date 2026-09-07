# Paper-Trading Validation

## Bounded synthetic run

Date: 2026-09-08

This validation uses `luv_staging_runner` with synthetic orders, simulated fills,
pre-trade risk checks, telemetry collection, and recovery-ledger replay. It does
not connect to an exchange and is not evidence of live-market or regulatory
readiness.

Command:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target luv_staging_runner --parallel 4
./build/luv_staging_runner \\
  --orders 150 \\
  --rate-hz 10 \\
  --ledger /tmp/luv_staging_150.bin
```

Observed result:

```text
synthetic telemetry: sent=150 approved=64 rejected=86 filled=64 exact=PASS
ledger replay ok=PASS count=64
staging_runner: final ok=PASS
```

Interpretation:

- All 150 submitted orders were accounted for.
- The 86 rejected orders were risk decisions, not execution or replay errors.
- Every approved order received a simulated fill.
- Recovery-ledger replay completed successfully and reconstructed 64 orders.
- This is a bounded synthetic gate, not a 48-hour run or a live paper-broker integration.
