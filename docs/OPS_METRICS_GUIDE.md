# LUV Observability Guide

## Prometheus scrape

The project now exposes a Prometheus-compatible metric block from the Telemetry layer. Use the scrape config in [docs/prometheus_luv_scrape.yml](docs/prometheus_luv_scrape.yml) and point it at the service that exposes `/metrics`.

For a local pilot/demo, start the runtime with:

```sh
./build/luv_engine --http-port 8080 --prometheus-port 9090 \
  --api-token "pilot-api-token" --metrics-token "pilot-metrics-token"
```

The metrics worker listens on `0.0.0.0:9090` and remains available until
shutdown. `/metrics` requires the configured bearer token; use
`curl -H 'Authorization: Bearer pilot-metrics-token' http://localhost:9090/metrics`.
The unauthenticated `/healthz` route is intended only for liveness/readiness
probes.

For Docker Compose, create separate untracked API and metrics token files.
The engine reads both at startup; Prometheus mounts only the metrics file, so
its scrape credential cannot authorize order-control requests:

```sh
mkdir -p secrets
printf '%s\n' 'pilot-api-token' > secrets/fort-api-token
printf '%s\n' 'pilot-metrics-token' > secrets/fort-metrics-token
printf '%s\n' 'replace-with-a-strong-grafana-password' > secrets/grafana-admin-password
docker compose up --build
```

## Example metrics

- `luv_execution_session_pnl`
- `luv_execution_gross_exposure`
- `luv_execution_fills_observed`
- `luv_execution_rejections_observed`
- `luv_execution_fills_total`
- `luv_execution_rejections_total`
- `luv_websocket_fill_notification_drops_total`
- `luv_execution_tick_rate_hz`
- `luv_execution_active_orders`
- `luv_execution_inference_latency_microseconds`
- `luv_execution_risk_check_latency_nanoseconds`
- `luv_telemetry_queue_depth`
- `luv_telemetry_dropped_snapshots_total`

The older `luv_session_pnl`, `luv_fill_count`, and related names remain as
deprecated compatibility aliases. Fill and rejection observations are gauges,
so do not apply `rate()` to them. Use the monotonic
`luv_execution_fills_total` and `luv_execution_rejections_total` counters for
rates and long-window totals.

## Example PromQL

- `rate(luv_execution_fills_total[5m])`
- `rate(luv_execution_rejections_total[5m])`
- `max_over_time(luv_execution_risk_check_latency_nanoseconds[5m])`
- `avg_over_time(luv_execution_active_orders[5m])`

## Grafana dashboard

Import [docs/grafana_luv_dashboard.json](docs/grafana_luv_dashboard.json) to visualize core runtime health. This dashboard is intentionally minimal and is meant to be the first monitoring surface for a pilot environment.

## Operational use

This is the basic operator view for a pilot deployment:

- alert on sustained rejection spikes
- alert on risk latency growth
- alert on orders halted or active-order drift
- correlate telemetry volume with processing health
