# LUV Observability Guide

## Prometheus scrape

The project now exposes a Prometheus-compatible metric block from the Telemetry layer. Use the scrape config in [docs/prometheus_luv_scrape.yml](docs/prometheus_luv_scrape.yml) and point it at the service that exposes `/metrics`.

For a local pilot/demo, start the runtime with `./build/main_engine --metrics`. It listens on `127.0.0.1:9090/metrics` and remains available until shutdown.

## Example metrics

- `luv_session_pnl`
- `luv_gross_exposure`
- `luv_fill_count`
- `luv_reject_count`
- `luv_tick_rate_hz`
- `luv_active_orders`
- `luv_inference_us`
- `luv_risk_ns`

## Example PromQL

- `rate(luv_fill_count[5m])`
- `rate(luv_reject_count[5m])`
- `max_over_time(luv_risk_ns[5m])`
- `avg_over_time(luv_active_orders[5m])`

## Grafana dashboard

Import [docs/grafana_luv_dashboard.json](docs/grafana_luv_dashboard.json) to visualize core runtime health. This dashboard is intentionally minimal and is meant to be the first monitoring surface for a pilot environment.

## Operational use

This is the basic operator view for a pilot deployment:

- alert on sustained rejection spikes
- alert on risk latency growth
- alert on orders halted or active-order drift
- correlate telemetry volume with processing health
