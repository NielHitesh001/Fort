# Known Issues (Parked Status)

This document records the operational limitations and current status of Fort as of its research park date.

| Topic | Parked status |
|---|---|
| WebSocket burst latency | Local notes only; CI macOS latency is **report-only**, not a product SLA |
| LOB depth | Max **16 orders per price level** |
| Active orders | **64 per symbol** |
| Synthetic feed | Execute/cancel/delete often **non-coherent** with book |
| Arena AI region | Prefers large mmap; may fall back to infrastructure-only |
| Arithmetic | **Hardened:** 17a-5, 15c3-1, 15c3-3. **Not uniformly hardened:** margin, FINRA, NMS, long tail |
| Risk stacks | Authoritative path = `ExecutionGateway` pre-trade; other risk headers are research |
| Deployment / live recovery | Not established |

---

## Detailed Observations

### 1. WebSocket Burst Latency
CI macOS latency runs are diagnostic and report-only; they do not establish a production SLA or performance guarantee. Burst latency measurements reflect local thread contention and host scheduling when hundreds of concurrent subscribers receive fills simultaneously.

### 2. Book Depth and Active Order Limits
- **LOB Depth:** Price levels are bounded to 16 orders per level (`Config::kMaxOrdersPerLevel`). Additional orders at that level are rejected.
- **Active Orders:** Bounded to 64 active orders per symbol (`Config::kMaxActiveOrders`). Exceeding this triggers symbol halt or rejection.

### 3. Synthetic Feed Coherence
The synthetic feed generator produces randomized ITCH events for throughput stress. Trade, cancel, and execute messages may not strictly match the state of resting orders in the reconstructed book.

### 4. Arena AI Region
Normal builds allocate a laptop-friendly 64MB AI model region by default. The full 13GB reservation is available only via `-DLUV_ENABLE_LARGE_AI_REGION=ON`. If mmap or mlock fails, Arena initialisation falls back to infrastructure-only mode.

### 5. Arithmetic Hardening
Checked arithmetic and bounds checking have been audited and verified for SEC 17a-5, 15c3-1, 15c3-3, and `ExecutionGateway` reservation math (`luv_numeric_limits.hpp`). Other standalone formulas across margin, FINRA, and Reg NMS remain exploratory research models without uniform bounds hardening.

### 6. Risk Architecture
`PreTradeRisk` within `ExecutionGateway` is the single authoritative pre-trade risk controller on the simulation engine path (`luv_engine`). Standalone headers (e.g., RTS 27/28, ISDA SIMM, FINRA 4210) are research units and are not wired into the gateway.

### 7. Deployment and Live Recovery
Distributed state synchronization, multi-region failover, and live market recovery mechanisms are simulation exercises and are not established for production use.
