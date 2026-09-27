# Backend Findings & UI Specification Harmonization (CHANGES.md)

This document records the exact findings from the codebase audit (`luv_*.hpp`, `docs/`, `test_*.cpp`) and details how the frontend UI ("Corridor") adheres to the backend reality rather than initial assumptions.

---

### 1. Limit Order Book Structure & Sort Order
- **Codebase Source**: `luv_lob.hpp` (lines 15, 33–50), `luv_arena.hpp` (lines 33–38).
- **Backend Reality**:
  - Bids are ordered **DESCENDING** (best bid at index 0).
  - Asks are ordered **ASCENDING** (best ask at index 0).
  - Maximum 16 orders per price level (`Config::kMaxOrdersPerLevel`).
  - Maximum 64 active orders per symbol (`Config::kMaxActiveOrders`).
  - Price levels use fixed-point arithmetic ($\text{price} \times 10^4$).
- **UI Implementation**: Order Book ladder displays Bids descending on the left and Asks ascending on the right, with depth percentage bars computed against the visible maximum volume. When the feed is offline or unverified, an explicit `<EmptyState>` is rendered to prevent showing fabricated depth.

---

### 2. Order Lifecycle Statuses & Rejection Reasons
- **Codebase Source**: `luv_execution.hpp` (lines 26–42), `luv_http_server.hpp` (lines 34–45), `luv_arena.hpp` (lines 194–206).
- **Backend Reality**:
  - The exact order lifecycle status values exposed by `HttpServer` and `ExecutionGateway` are:
    `pending`, `live`, `partial`, `cancelled`, `done`, `rejected`, `not_found`.
  - Rejection bitmask in `luv_execution.hpp` (`exec::Reject`):
    - `kRejectQty` (bit 0): Quantity exceeds max limits or zero.
    - `kRejectPosition` (bit 1): Position or gross exposure cap breached.
    - `kRejectStaleAlpha` (bit 2): Alpha signal age exceeds 250 μs threshold.
    - `kRejectHalted` (bit 3): Symbol trading halted.
    - `kRejectFlatSignal` (bit 4): Alpha signal direction is flat.
    - `kRejectOrderCapacity` (bit 5): Symbol active order capacity (64) reached.
    - `kRejectInvalidSymbol` (bit 6): Symbol index out of range (0..511).
    - `kRejectPrice` (bit 7): Price out of valid collar / limits.
- **UI Implementation**: Exact enum values mapped 1:1 to `<StatusChip>` components. Rejected orders display the specific underlying rejection reason from the backend. "Execution transmission timeout" (HTTP 504) is handled as a distinct gateway timeout.

---

### 3. Order Types & Entry Form Constraints
- **Codebase Source**: `README.md` (Known Limitations), `luv_execution.hpp`, `luv_http_server.cpp` (lines 131–160).
- **Backend Reality**:
  - Authoritative `ExecutionGateway` over HTTP/REST supports **market** and **limit** orders only.
  - Complex conditional triggers (stop-loss, pegged orders) exist in `luv_execution.hpp` for internal simulation but are not exposed on the REST control plane.
- **UI Implementation**: Order entry form strictly provides Market and Limit orders without fabricating un-routable conditional or stop order options.

---

### 4. Telemetry Metrics vs. Quantile Expositions
- **Codebase Source**: `luv_telemetry.hpp`, `test_prometheus_export.cpp`, `docs/OPS_METRICS_GUIDE.md`.
- **Backend Reality**:
  - Prometheus exporter on `:9090/metrics` exposes the following gauges and counters:
    - `luv_execution_session_pnl` (gauge)
    - `luv_execution_gross_exposure` (gauge)
    - `luv_execution_fills_observed` (gauge)
    - `luv_execution_rejections_observed` (gauge)
    - `luv_execution_fills_total` (monotonic counter)
    - `luv_execution_rejections_total` (monotonic counter)
    - `luv_websocket_fill_notification_drops_total` (monotonic counter)
    - `luv_execution_tick_rate_hz` (gauge)
    - `luv_execution_active_orders` (gauge)
    - `luv_execution_inference_latency_microseconds` (gauge)
    - `luv_execution_risk_check_latency_nanoseconds` (gauge)
    - `luv_telemetry_queue_depth` (gauge)
    - `luv_telemetry_dropped_snapshots_total` (monotonic counter)
  - The endpoint does **not** expose precomputed rolling P50/P99 latency histogram buckets or N=100 fanout burst percentiles.
- **UI Implementation**: Accurately plots observed risk check and inference latency gauges and tick rate time series. The absence of server-side quantile histogram buckets is explicitly stated with `<EmptyState>` / coverage badges rather than synthesizing fake percentiles.

---

### 5. Memory Arena Pressure & Circuit Breaker States
- **Codebase Source**: `luv_arena.hpp` (lines 304–340), `luv_safety.hpp` (lines 581–586).
- **Backend Reality**:
  - Memory Pressure Tiers:
    - `< 70%`: `kNormal`
    - `≥ 70%`: `kWarning`
    - `≥ 85%`: `kShedLoad` (only cancellations admitted)
    - `≥ 95%`: `kEmergencyHalt` (hard fail, all ingress rejected)
  - Circuit Breaker States: `kClosed` (normal), `kOpen` (tripped/halted), `kHalfOpen` (probing).
- **UI Implementation**: System Health ops panel implements these exact tiers with escalation to `--status-warn` at 70% and `--status-critical` at 85%/95%.

---

### 6. Audit Trail & Regulatory Scope
- **Codebase Source**: `luv_safety.hpp` (`AuditEvent`), `luv_recovery.hpp` (`RecoveryLedger`), `docs/AUDIT_LOGGING.md`.
- **Backend Reality**:
  - Durable audit log uses local SHA-256 hash chaining for accidental corruption detection.
  - It does **not** provide SEC Rule 17a-4, FINRA, or CAT compliance.
- **UI Implementation**: The Audit screen provides a paginated viewer with CSV export and an uncompromised disclaimer regarding research boundaries.

---

### 7. Hardening & Bugfix Pass: Independent Signals vs. Bugs

| Item | Status | Root Cause & Resolution |
| :--- | :--- | :--- |
| **Bug 1: Order Book & Audit Trail showing fake live data while offline** | **Fixed (Bug)** | Hardcoded mock state arrays in `useState` and fallback rows (`query.data?.rows || [...]`) bypassed connection-state gates. **Fixed**: Replaced with strict connection-state checks (`useConnectionState`), rendering `<EmptyState>` when offline or unverified. |
| **Bug 2: System Health disagreeing connectivity indicators** | **Clarified & Harmonized (Real Signals + UI Fix)** | The 4 signals (`/healthz` HTTP probe, RFC 6455 WebSocket stream, ITCH 5.0 Feed ingestion state, and Global status) are genuinely independent subsystems: (1) HTTP health probe tests port 9090; (2) WebSocket transport tests port 8080 stream; (3) Feed state tests tick rate arrival. **Fixed**: Fixed hardcoded `wsConnected=true` and `feedFresh=true` assumptions, wired them to real scrape/transport states, and added an explicit *Subsystem Signal Notice* callout explaining divergence when transport is connected while HTTP/Feed stalls. |
| **Bug 3: Tick Rate vs Last Tick Update contradiction** | **Fixed (Bug)** | `lastUpdate` was set to current clock time on component mount rather than being bound to actual tick ingestion events. **Fixed**: Bound `lastTickTime` directly to Prometheus scrapes where `luv_execution_tick_rate_hz > 0`; displays `"No ticks observed"` when rate is 0 or offline. |
| **Bug 4: Ambiguous client-side breaker toggle** | **Fixed (Improvement)** | Breaker toggle button lacked visual differentiation from real engine action controls. **Fixed**: Created shared `<DemoOnlyControl>` with dashed border, amber badge (`DEMO / UI-ONLY`), and tooltip explaining it operates exclusively in local browser state. |
| **Bug 5: Static "NO TELEMETRY SCRAPE YET" header note** | **Fixed (Improvement)** | Static text was displayed regardless of active route or connection state. **Fixed**: Header note is now screen-aware, rendering `"ENGINE OFFLINE · NO ACTIVE SCRAPE"`, `"LAST SCRAPE: Xs AGO"`, or `"DIRECT API PROBE MODE"`. |
| **Part 2: Sandbox / API Console (`/sandbox`)** | **Implemented (Feature)** | Added 7th screen featuring raw REST test client, verbatim Prometheus `:9090/metrics` fetcher, live RFC 6455 WebSocket inspector, connection store debug panel, and environment readout. Marked with persistent `INTERNAL TOOL` banner. |
| **Part 3: Unified Connection State & Regression Test** | **Implemented (Hardening)** | Extracted `useConnectionState()` in `lib/connection.ts` as the single source of truth across all 7 screens. Added automated Playwright test verifying that when offline, no screen displays fabricated numbers. |
