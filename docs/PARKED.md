# Project Status: Parked

**Park Date:** September 16, 2026  
**Status:** FROZEN RESEARCH SIMULATOR (PARKED)

---

## 1. Why Fort is Parked

Fort was developed as a research and simulation framework to study in-process limit order book (LOB) mechanics, low-latency C++20 systems techniques, and market microstructure models.

The owner has achieved the core research milestone and is focusing efforts elsewhere. Fort is brought to a clean, stable, frozen research state and parked. No further active feature development, live-capital expansion, or commercial productization is planned.

---

## 2. What Is Supported vs. Unsupported

### Supported Path
- **Engine Core:** Building and running `luv_engine` (in-process simulation matching engine).
- **Control Plane (Simulation):** Local HTTP/WebSocket simulation control interface (`127.0.0.1`).
- **Core Test Suite:** Verification targets covering LOB matching, feed decode, execution gateway, sequence tracker, crash recovery, and socket transport.
- **Platform Support:** macOS (primary development & testing) and Ubuntu 24.04 (secondary CI).

### Unsupported
- **Live Trading:** Connecting to live exchanges, executing real trades, or managing financial capital.
- **Research Modules as Production:** Treating standalone research headers (e.g. CFTC Rule 575, ESMA MAR, ISDA SIMM, Raft clustering, exotic options pricing) as unified or certified production engines.
- **Commercial Terminal / SaaS:** Any Bloomberg-like GUI, multi-tenant cloud offering, or subscription data feed.
- **SLAs / Support:** There is no service-level agreement for issues or pull requests; maintainers may not respond.

---

## 3. How to Un-Park Later

If development is ever resumed, the owner must establish a written product or research specification before un-freezing the repository:

1. **Explicit Goal Definition:** Define whether Fort is being revived as an execution research library, a simulation bench, or a specific tooling module.
2. **Strict Scope Control:** Do not attempt to make Fort "everything at once" (avoid mixing exchange gateways, regulatory filings, pricing libraries, and GUI terminals into a single monolithic tree).
3. **Formal Verification Gate:** If live execution is ever contemplated, full independent external security/compliance auditing and exchange certification must occur before handling capital.
