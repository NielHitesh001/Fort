# Failure modes and recovery evidence

Scope: simulator components; no live-venue recovery claim. Reviewed 2026-09-14.

| Failure | Existing evidence | Operational boundary |
| --- | --- | --- |
| Missing/out-of-order feed sequence | test_sequence_tracker.cpp, test_moldudp64_fixture.cpp | Component detection/retransmit behavior; not automatic live reconnect in main_engine |
| Local ledger corruption or partial write | test_crash_recovery.cpp, test_crash_recovery_process.cpp | Preserve original ledger; reject corrupt replay and investigate before resuming |
| Memory pool exhaustion | test_mempool.cpp; arena layout tests in test_arena.cpp | Different pools have different rejection/recycle semantics; no universal manual-restart claim |
| HTTP/WS shutdown and slow clients | test_http_server.cpp, test_websocket.cpp | Bounded socket state; failing latency assertion may prevent later WS scenarios from executing |
| Regional network partition | test_multiregion_raft_cluster.cpp | Research model; no integrated deployment failover |

`test_feed_disconnect_partial_fill.cpp` pauses a synthetic source through the test
harness while a gateway order is half-filled, verifies persisted remaining quantity,
resumes input and completes the fill. It models absence of feed messages, not a
physical socket reconnect. `test_arena_exhaustion_recovery.cpp` fills the 64-slot
per-symbol execution slab, verifies rejection leaves the partial fill and risk
reservations intact, then checks the latched halt, explicit `resume_symbol`, slot
reuse and ledger reopen/replay. Run `./run_failure_tests.sh`.

Live replay arbitration still needs an implemented ownership/handoff protocol;
the executable has no simultaneous live/replay mode to test. No test claiming that
deployment scenario is supplied. Raft deployment split-brain testing is conditional
on integration; no such runtime integration was established.

Safe research restart procedure: stop admission, retain raw logs and configuration,
stop producers before consumers/storage, inspect persistence integrity, replay into
isolated state where supported, reconcile IDs and remaining quantities, then resume
synthetic input. Do not treat this as an implemented automatic failover protocol.
The partial-fill test prints elapsed local diagnostic time; no deployment MTTR is
promised. It includes local ledger work and is not a published benchmark.
