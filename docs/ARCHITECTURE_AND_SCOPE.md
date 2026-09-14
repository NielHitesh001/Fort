# Architecture and scope

Source audit: 2026-09-14, baseline `cdae713`. Integration below describes the
`luv_engine` executable, not every standalone test or research module.

```text
SimFeedSource (luv_feed_sim.hpp) -> Arena tick ring -> Consumer
                                                      | LOB / features / optional AI
                                                      v
REST -> ExecutionBridge SPSC -> ExecutionGateway -> outbound SPSC -> optional localhost UDP
                                  | fill callback
                                  v
                             WebSocket SPSC -> socket worker
Execution owner -> Arena telemetry SPSC -> MetricsHttpServer
```

`main_engine.cpp` constructs these components. `luv_consumer.hpp` includes
`luv_lob.hpp`, `luv_features.hpp` and `luv_ai.hpp`. `luv_execution.hpp` owns risk,
packet construction and reconciliation and includes `luv_recovery.hpp` and
`luv_safety.hpp`. Inclusion of a recovery class alone does not mean its persistence
is configured in the executable. HTTP and WebSocket have separate implementation
libraries. Metrics run on a separate thread. REST fills are delayed synthetic
execution reports, not exchange acknowledgements.

`luv_arena.hpp` provides bounded preallocated storage on critical paths; this is
not a claim that the entire process, startup or every test avoids allocation.
CMake requires OpenSSL Crypto. The project is not dependency-free.

The optional Linux DPDK build uses `packet_io_dpdk.c` instead of the stub.
`luv_feed_dpdk.hpp` and `luv_decode_itch.hpp` exist, but enabling DPDK does not
replace the SimFeedSource constructed by main_engine with a certified live venue.

Representative research modules `luv_heston_pricer.hpp`, `luv_bates_pricer.hpp`,
`luv_sec_rule_17a5.hpp`, `luv_irs.hpp`, `luv_cds.hpp` and
`luv_multiregion_raft_cluster.hpp` have standalone test targets. No direct use of
these classes was found in the main engine pipeline reviewed here. They are
research implementations, not deployed compliance or failover services. This is
not a complete transitive dependency certification of all 151 headers.

Fort supports local simulation, educational experiments and strategy prototyping.
It does not establish live customer execution, custody, clearing, regulatory
reporting or operational multi-region failover. Parser tests and arithmetic tests
provide specific evidence only; coverage quality across all modules is unverified.
