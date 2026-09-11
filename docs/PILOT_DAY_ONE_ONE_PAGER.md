# LUV Flicker Pilot

## A monitorable trading-system integration surface on day one

LUV Flicker is a C++20, simulation-oriented market-data and execution framework with a live Prometheus-compatible metrics endpoint. A pilot operator can start the runtime, inspect metrics directly, and load a Grafana dashboard without building a custom monitoring adapter.

## Day-one operator flow

```mermaid
flowchart LR
    A[Start simulation runtime] --> B[Authenticated HTTP /metrics on 0.0.0.0:9090]
    B --> C[Prometheus scrape]
    C --> D[Grafana dashboard]
    B --> E[Direct curl verification]
    A --> F[Graceful shutdown]
```

## Start the pilot

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
./build/luv_engine --http-port 8080 --prometheus-port 9090 \
  --api-token 'pilot-api-token' --metrics-token 'pilot-metrics-token'
```

Verify the live surface:

```sh
curl -H 'Authorization: Bearer pilot-metrics-token' \
  http://127.0.0.1:9090/metrics
```

Then use:

- [Prometheus scrape config](prometheus_luv_scrape.yml)
- [Grafana dashboard](grafana_luv_dashboard.json)
- [Operator metrics guide](OPS_METRICS_GUIDE.md)

## What the operator sees

The endpoint exposes the core runtime signals needed for a first pilot:

- session PnL and gross exposure
- fill and rejected-order counters
- active orders
- tick rate
- inference latency
- risk-check latency
- halted state through the runtime telemetry model

`/metrics` is intentionally bearer-protected; an unauthenticated request
returns `401`. The separate `/healthz` endpoint is unauthenticated only for
orchestrator liveness and readiness checks. The process shuts down cleanly
after `Ctrl-C`.

## Evidence available today

- Real TCP HTTP server, not only a renderer function
- Prometheus exposition format verified by an integration test
- Live scrape verified with `curl`
- Grafana import artifact included
- Operator runbook included
- Clean shutdown path exercised during the live run

## Pilot boundary

This package supports controlled integration testing and operational discovery. It is not a production trading approval, exchange certification, or recommendation to connect real capital. Before production use, independently complete exchange integration, packet-gap recovery, authenticated execution reconciliation, external audit retention, failover testing, hardware validation, and required legal and risk reviews.

## Suggested pilot conversation

> Here is the first-day operator experience: start the simulation, scrape the runtime, import the dashboard, and inspect live order, fill, exposure, and risk-latency signals. The next step is to agree on one controlled integration workflow and the evidence required to graduate it.
