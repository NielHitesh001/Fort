# Known issues

## WebSocket burst latency

Locally resolved on macOS in `9e5f353`; platform/load validation remains open.
See [measurements and reproduction](PERFORMANCE_MEASUREMENTS.md).
Historical context: `test_one_hundred_order_fill_burst` in
`test_websocket.cpp` checks 100 disjoint fills with the configured 1,000-connection
pool (900 unrelated subscribers). Prior local macOS runs aborted at the P99
100 microsecond assertion, reporting approximately 300–365 microseconds. These
are historical diagnostic observations without a complete retained host manifest.

`cdae713` fixes the macOS compile and poll-registration defects; it does not close
the burst-performance issue. The SEPT_11_STATUS claims of passing tests/sanitizers
are historical and cannot establish the current checkout's status.

Reproduce with `cmake --build build --target luv_websocket` then
`ctest --test-dir build -R '^websocket$' --output-on-failure`. Investigate worker
wakeup, full-pool readiness scans and scheduling under bursts. Retain full output
and host metadata before claiming the target is met. Functional tests after an
aborting latency assertion have not run in that invocation.

## Arithmetic review scope

SEC 17a-5 now rejects out-of-domain inputs and overflowing intermediates through
`arithmetic_valid=false`, with approval flags cleared. SEC 15c3-1 and 15c3-3 also
have checked arithmetic and boundary tests. Other financial/regulatory
modules remain individually unverified for complete boundary safety. Passing one
module's tests does not validate their arithmetic or regulatory assumptions.

## Deployment recovery and external validation

The executable uses a simulated feed. Live-feed recovery arbitration, regulatory
reporting and distributed failover are not established by the presence of research
classes. See FAILURE_MODES.md and EXTERNAL_REVIEW_ROADMAP.md.
