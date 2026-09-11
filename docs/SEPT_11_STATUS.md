# Sept 11 WebSocket Sprint — End of Day Status

## Blocker (Needs Decision)
- **WebSocket N=100 burst latency:** P99 = ~308μs (target: <100μs)
- **Root cause:** Syscall overhead per subscriber in broadcast loop
- **Fix status:** Event-driven poll + batch queue partially implemented
- **Next step:** Remeasure after HTTP hardening edits complete

## What's Done
✅ REST API (POST/GET/DELETE orders, GET positions)
✅ WebSocket RFC 6455 (handshake, frames, lifecycle)
✅ 13 core scenarios (happy path, backpressure, pool exhaustion)
✅ Build clean (no warnings)
✅ Sanitizers passing (ASan/UBSan/TSan)
✅ Prometheus metrics (0.0.0.0:9090 + auth)
✅ K8s YAML validated

## Decisions Needed (Niel)
1. Accept P99 ~300μs for WebSocket fanout, or keep strict <100μs?
2. Deployment priority: local / Docker Compose / Kubernetes?
3. Next sprint scope: compliance (AML/KYC/audit) or live venue integration?

## Resources for Next Engineer
- Current branch: main (clean, awaiting burst remeasure)
- Test command: `ctest --test-dir build -R websocket`
- Bench command: `./build/test_websocket --bench`
- Live test: See test_http_server.cpp, lines 60–105 (REST→WS fill flow)

## Known Limitations
- LeakSanitizer unavailable on macOS (ASan/UBSan/TSan available)
- Kubernetes manifests YAML-valid but untested on live cluster
- Deployment docs: docker-compose tested, K8s deployment untested
