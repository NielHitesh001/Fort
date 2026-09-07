# Arbor / LUV Flicker

Arbor is the repository and project identity; LUV Flicker is the current C++
engine name. It is a research-grade C++20 library for reconstructing a Nasdaq
ITCH-style limit order book from preallocated memory. It is not production
trading software and must not be connected to real capital without an
independent review, exchange certification, reconciliation, and operational
controls.

## Evidence Boundary

Validation claims are tracked under the CP-003 evidence contract in
`docs/CP-003_EVIDENCE_PLAN.md`. Local test passes establish only the stated
software invariant under the recorded environment; they do not establish
exchange certification, target-hardware performance, operational readiness, or
permission to trade real capital.

## Scope

- Header-only LOB, ITCH decoder, feature, execution, and telemetry components
- Simulation and replay-oriented feed paths
- Fixed-width integer prices and quantities
- Preallocated `mmap` arena and bounded SPSC rings
- Bounded OUCH-style response parsing for local flow tests
- Optional experimental AI model loading, disabled for shared objects unless
	`LUV_ENABLE_DYNAMIC_MODEL_LOADING` is explicitly defined

This repository does not provide a certified exchange gateway, durable
regulatory retention service, portfolio ledger, sequence-gap recovery, or a
complete production deployment. `main_engine.cpp` is a simulation example.

## Safety Contracts

The feed and LOB are single-writer components. Exactly one ingestion/consumer
thread may mutate the LOB and its `OrderRefMap`. Query accessors must not run
concurrently with mutation unless the application supplies synchronization.

The tick and telemetry queues are strict SPSC rings: exactly one producer may
claim/commit and exactly one consumer may peek/consume each ring. Violating
this contract is a data race.

Malformed ITCH messages are rejected. LOB handlers validate map locations,
symbol and slot bounds, duplicate references, and aggregate quantities before
dereferencing or subtracting state.

Execution admission validates symbol, side, price, quantity, client order ID,
alpha age, position limits, and active-order capacity. Capacity exhaustion
halts the affected symbol and trips an execution circuit breaker. The library
also provides `SequenceTracker`, `ReconciliationLedger`, `OrderRateLimiter`,
`DurableAuditLog`, and `ShutdownController` primitives for application
integration. The DPDK feed enforces MoldUDP64 sequence continuity when built
with `LUV_USE_DPDK`; the non-DPDK stub reports sequence health as unavailable.
These primitives do not by themselves provide exchange reconciliation or
regulatory retention.

`luv_ouch.hpp` defines a small framed adapter for local integration tests. Its
message layout is documented in the header and is not a venue-certified OUCH
implementation or a replacement for a broker-specific parser.

## Build and Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

For sanitizer validation:

```sh
cmake -S . -B build-asan \
	-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j2
ctest --test-dir build-asan --output-on-failure
```

Use `-DLUV_REQUIRE_MLOCK=ON` only on a host configured to permit memory
locking. When enabled, arena initialization fails if infrastructure memory
cannot be locked; the default simulation behavior is best effort.

## Experimental AI Loader

The model loader validates file size and metadata, reads bytes into the
already-owned arena, and never uses `MAP_FIXED`. Dynamic shared-object loading
is opt-in and requires an absolute, resolved, non-group/world-writable file
owned by the effective user. Validate model output and isolate artifacts before
enabling this path.

## Production Gate

Before any real-money use, independently complete and document:

- Exchange-certified feed and order gateway integration
- ITCH sequence tracking, duplicate detection, and gap recovery
- Durable append-only audit retention and external backup
- Reconciliation against exchange execution reports
- Explicit shutdown, cancel, and failover procedures
- Concurrency design and tests for every reader/writer boundary
- Sanitizer, fuzz, load, and hardware-specific latency testing
- Risk limits, circuit breakers, operator alerts, and credential controls

See `VALIDATION_SPRINT_LOG.md` for evidence tracking when that file is present.
