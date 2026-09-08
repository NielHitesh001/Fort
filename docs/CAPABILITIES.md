# Fort Capabilities & Scope Specification

This document details the functional capabilities included in Fort, as well as the out-of-scope production and regulatory components.

---

## 1. Included Capabilities

### A. High-Frequency Market Microstructure & Order Book Simulation
- **Nasdaq ITCH 5.0 Protocol Decoder**: Binary protocol parser for System Event, Stock Directory, Trading Action, Add Order, Add Order with MPID, Trade, Cross Trade, Broken Trade, and NOII messages.
- **In-Memory Limit Order Book (LOB)**: Cache-aligned, multi-level price-time FIFO matching engine supporting limit, market, pegged, iceberg, cancel, and cross-order workflows.
- **Zero-Allocation Memory Arena**: Pre-allocated contiguous memory pools (`luv_arena.hpp`) eliminating heap fragmentation and dynamic runtime allocation on critical paths.
- **Multi-Level Order Flow Imbalance (OFI)**: Real-time calculation of Cont-Kukanov-Stoikov depth imbalance and Rama Cont multi-horizon order flow vectors.
- **Micro-Price & Slippage Estimators**: Continuous computation of volume-weighted micro-price, bid-ask spreads in basis points, and book slippage models.

### B. Algorithmic Execution & Market Making Solvers
- **Almgren-Chriss Optimal Execution**: Dynamic programming solver minimizing risk-adjusted implementation shortfall with linear temporary and permanent price impact.
- **Cartea-Jaimungal Alpha Quoting Engine**: Asymmetric market making model incorporating external alpha drift and terminal inventory risk aversion.
- **Guéant-Tapia-Manziadi Market Making Model**: Closed-form approximations for optimal bid-ask quoting spreads under exponential intensity arrival.
- **VWAP & TWAP Schedulers**: Uniform time-sliced TWAP and U-shaped volume-profile VWAP execution slicing with volume-participation constraints.
- **Statistical Arbitrage & OU Process Solvers**: Mean-reverting Ornstein-Uhlenbeck parameter estimation and entry/exit threshold solvers.

### C. Quantitative Derivatives & Volatility Pricing
- **Black-Scholes & Greeks Engine**: Analytical Delta, Gamma, Vega, Theta, Rho solvers with exact Put-Call Parity validation.
- **Heston (1993) Stochastic Volatility Model**: Semi-analytical option pricing via Gauss-Legendre characteristic function inversion.
- **Bates (1996) Stochastic Volatility Jump-Diffusion**: Merton-style log-normal Poisson jump diffusion combined with continuous stochastic variance.
- **Rough Bergomi (rBergomi) Model**: Fractional Brownian volatility modeling capturing empirical power-law explosion in short-dated ATM volatility skew ($H < 0.5$).
- **Variance Swaps & Cross-Currency Pricing**: Fair strike pricer for variance swaps, FX forward points, and Covered Interest Parity (CIP) solvers.

### D. Regulatory & Surveillance Simulation Frameworks
- **SEC Rule 201 Short Sale Alternative Uptick Engine**: 10% intraday circuit breaker trigger with strict $P > NBB$ order pricing restrictions.
- **SEC Rule 10b-18 Safe Harbor Quoter**: Volume (25% ADTV), timing, price, and single-broker buyback constraints.
- **CFTC Rule 575 Anti-Spoofing & ESMA MAR Surveillance**: Pattern detection for layering, spoofing, quote stuffing (>4,000 Hz), wash trading, and marking the close.
- **Regulatory Capital & Reporting Math**: Mathematical formulations for SEC Rule 15c3-1 net capital, 15c3-3 customer reserve formula, 17a-5 BD audit evaluation, ISDA SIMM v2.6 initial margin, and MiFID II RTS 25 clock synchronization SLA.

### E. Infrastructure, Reliability & Benchmarking
- **Lock-Free Telemetry Ring Buffer**: Sub-microsecond latency recording and percentile export (p50, p90, p99, p99.9).
- **Multi-Region Raft Cluster Simulation**: Active-active cross-datacenter state machine replication (NY4, LD4, TY3) with Hybrid Logical Clock (HLC) causality.
- **Hardware Watchdog & Circuit Breakers**: Multi-tier price collars, kill switches, and hardware heartbeat watchdogs.
- **Optional DPDK Kernel Bypass Packet I/O**: High-throughput network backend for Linux environments (stubbed for macOS/POSIX).

---

## 2. Excluded / Out-of-Scope Components

The following components are **NOT** included in this educational framework and must be implemented before any live deployment:

| Excluded Component | Status | Production Requirement |
| :--- | :---: | :--- |
| **Live Exchange Multicast Connectivity** | Excluded | Licensed market data feeds from Nasdaq/Cboe/CME. |
| **Live Direct Market Access (DMA) Routing** | Excluded | Production OUCH / FIX binary order gateway sessions. |
| **Physical Clearing & Settlement (T+1 / T+2)** | Excluded | Integration with DTCC/NSCC/Euroclear clearinghouses. |
| **Live Financial Custody & Banking Rails** | Excluded | Multi-currency custodial banking and fiat settlement rails. |
| **Statutory AML/KYC & FinCEN SAR Filing** | Excluded | Certified identity verification and SAR automated filing. |
| **Third-Party Regulatory Audit Submissions** | Excluded | PCAOB independent auditor filings via SEC EDGAR. |
| **Production Hardware HSM Key Management** | Excluded | FIPS 140-2 Level 3 hardware security modules. |
| **REST / WebSocket Public Gateway** | Excluded | External HTTP/WebSocket API server endpoints. |

---

## 3. Intended Use Cases

- **Academic & Research Exploration**: Studying market microstructure dynamics, Order Flow Imbalance, and stochastic volatility models.
- **Quantitative Algorithm Testing**: Developing and backtesting algorithmic execution and market making strategies against realistic simulated order books.
- **Systems & Performance Benchmarking**: Understanding low-latency C++ techniques (cache alignment, zero-allocation memory pools, branch prediction optimization).
