# Fort Capabilities & Scope Specification

This document details the functional capabilities included in Fort, as well as out-of-scope production and regulatory components.

Fort is a **research and simulation** framework. Meeting local measurements or green CI does not establish production readiness or suitability for live capital.

---

## 1. Included Capabilities

### A. High-Frequency Market Microstructure & Order Book Simulation (Core Engine)
- **Nasdaq ITCH 5.0 Protocol Decoder**: Binary protocol parser for System Event, Stock Directory, Trading Action, Add Order, Add Order with MPID, Trade, Cross Trade, Broken Trade, and NOII messages.
- **In-Memory Limit Order Book (LOB)**: Cache-aligned, multi-level price-time FIFO matching engine supporting limit, market, pegged, iceberg, cancel, and cross-order workflows.
- **Zero-Allocation Memory Arena**: Pre-allocated contiguous memory pools (`luv_arena.hpp`) eliminating heap fragmentation and dynamic runtime allocation on critical paths. Default laptop-friendly 64MB AI budget.
- **Multi-Level Order Flow Imbalance (OFI)**: Real-time calculation of Cont-Kukanov-Stoikov depth imbalance and Rama Cont multi-horizon order flow vectors.
- **Micro-Price & Slippage Estimators**: Continuous computation of volume-weighted micro-price, bid-ask spreads in basis points, and book slippage models.
- **Pre-Trade Risk & Execution Gateway**: Authoritative simulation risk checks (`PreTradeRisk` in `luv_execution.hpp`) with position caps, collar checks, circuit breakers, and rate limiters.
- **Local Simulation Control Plane**: Local loopback HTTP server and WebSocket fill/fanout path (defaults to loopback `127.0.0.1`; **simulation only**). This is **not** a public API, venue gateway, or DMA session.

### B. Research Modules (Optional Libraries under `LUV_TESTS`, Not Wired into `luv_engine`)
The following modules exist as standalone research models and test suites. They are **not** wired into the `luv_engine` matching loop:
- **Algorithmic Execution & Market Making Solvers**: Almgren-Chriss optimal execution, Cartea-Jaimungal alpha quoter, Guéant-Tapia-Manziadi model, VWAP/TWAP schedulers, Ornstein-Uhlenbeck stat arb.
- **Quantitative Derivatives & Volatility Pricing**: Black-Scholes Greeks, Heston (1993) stochastic volatility, Bates (1996) jump-diffusion, Rough Bergomi (rBergomi), FX CIP forward points, variance swaps.
- **Regulatory & Surveillance Simulation Math**: SEC Rule 15c3-1 net capital, 15c3-3 customer reserve, 17a-5 BD audit calculations (hardened bounds); CFTC Rule 575 anti-spoofing and ESMA MAR surveillance pattern detectors.
- **Distributed Simulation Experiments**: Multi-region Raft cluster simulation with simulated DC latencies; toy Smart Order Router (SOR) prototypes.

---

## 2. Excluded / Out-of-Scope Components

The following components are **NOT** included in this research simulator and must not be treated as live-trading infrastructure:

| Excluded Component | Status | Production Requirement |
| :--- | :---: | :--- |
| **Live Exchange Multicast Connectivity** | Excluded | Licensed market data feeds from Nasdaq/Cboe/CME. |
| **Live Direct Market Access (DMA) Routing** | Excluded | Production OUCH / FIX binary order gateway sessions. |
| **Public Venue Gateway** | Excluded | Public-facing exchange or broker gateway endpoints. Local loopback control plane is for simulation only. |
| **Physical Clearing & Settlement (T+1 / T+2)** | Excluded | Integration with DTCC/NSCC/Euroclear clearinghouses. |
| **Live Financial Custody & Banking Rails** | Excluded | Multi-currency custodial banking and fiat settlement rails. |
| **Statutory AML/KYC & FinCEN SAR Filing** | Excluded | Certified identity verification and SAR automated filing. |
| **Third-Party Regulatory Audit Submissions** | Excluded | PCAOB independent auditor filings via SEC EDGAR. |
| **Production Hardware HSM Key Management** | Excluded | FIPS 140-2 Level 3 hardware security modules. |

---

## 3. Intended Use Cases

- **Academic & Research Exploration**: Studying market microstructure dynamics, Order Flow Imbalance, and stochastic volatility models.
- **Quantitative Algorithm Testing**: Developing and backtesting algorithmic execution and market making strategies against realistic simulated order books.
- **Systems & Performance Benchmarking**: Understanding low-latency C++ techniques (cache alignment, zero-allocation memory pools, branch prediction optimization).

Latency numbers in `docs/PERFORMANCE_MEASUREMENTS.md` are host-specific local notes; CI macOS latency is report-only and is not a product SLA.
