# Research Modules Index

The targets listed below exist within the repository and build under CMake test targets (`LUV_TESTS`), but they are **research models and academic prototypes**, **NOT** parts of the core `luv_engine` product.

They are retained in a frozen research state for reference, algorithmic exploration, and independent unit testing.

---

## 1. Algorithmic Execution & Market Making Prototypes
- **Almgren-Chriss (`luv_almgren_chriss.hpp`, `test_almgren_chriss.cpp`)**: Optimal execution implementation shortfall solver.
- **Cartea-Jaimungal (`luv_cartea_jaimungal.hpp`, `test_cartea_jaimungal.cpp`)**: Asymmetric market making model with inventory penalty and alpha drift.
- **Guéant-Tapia-Manziadi (`luv_gueant_tapia_manziadi.hpp`, `test_gueant_tapia_manziadi.cpp`)**: Closed-form exponential arrival market maker.
- **VWAP / TWAP (`luv_vwap_twap_execution.hpp`, `test_vwap_twap_execution.cpp`)**: Volume-sliced order scheduler prototypes.
- **Statistical Arbitrage / OU (`luv_stat_arb_ou.hpp`, `test_stat_arb_ou.cpp`)**: Ornstein-Uhlenbeck mean-reverting pair model.

## 2. Quantitative Derivatives & Volatility Models
- **Black-Scholes & Greeks (`luv_greeks.hpp`, `test_greeks.cpp`)**: Analytical Greeks calculations.
- **Heston (1993) (`luv_heston_pricer.hpp`, `test_heston_pricer.cpp`)**: Semi-analytical stochastic volatility solver.
- **Bates (1996) (`luv_bates_pricer.hpp`, `test_bates_pricer.cpp`)**: Stochastic volatility with jump diffusion.
- **Rough Bergomi (`luv_rough_bergomi_pricer.hpp`, `test_rough_bergomi_pricer.cpp`)**: Fractional Brownian motion volatility pricer.
- **Swaps & Fixed Income (`luv_cds.hpp`, `luv_irs.hpp`, `luv_yield_curve.hpp`)**: Academic credit and rate curve pricers.

## 3. Regulatory & Capital Math Prototypes
- **SEC Net Capital 15c3-1 (`luv_net_capital_15c3_1.hpp`, `test_net_capital_15c3_1.cpp`)**: Capital requirement calculator (hardened bounds).
- **SEC Customer Reserve 15c3-3 (`luv_customer_protection_15c3_3.hpp`, `test_customer_protection_15c3_3.cpp`)**: Reserve deposit formula (hardened bounds).
- **SEC Form 17a-5 (`luv_sec_rule_17a5.hpp`, `test_sec_rule_17a5.cpp`)**: BD audit calculation model (hardened bounds).
- **FINRA 4210 / Margin (`luv_finra_4210.hpp`, `luv_margin.hpp`, `test_finra_4210.cpp`)**: Margin computation models.
- **CFTC Rule 575 / ESMA MAR (`luv_cftc_rule_575.hpp`, `luv_esma_mar_surveillance.hpp`)**: Pattern matching simulations for spoofing, wash sales, and layering.
- **MiFID II RTS 27/28 (`luv_best_execution_rts27.hpp`, `luv_rts28.hpp`)**: Best execution reporting simulation.

## 4. Distributed & Infrastructure Experiments
- **Multi-Region Raft Simulation (`luv_multiregion_raft_cluster.hpp`, `test_multiregion_raft_cluster.cpp`)**: Simulated consensus across simulated DC latencies.
- **SOR / Routing Prototypes (`luv_sor_smart_router.hpp`, `test_sor_smart_router.cpp`)**: Toy smart order router simulation.

---

> [!NOTE]
> None of these research modules are connected to the live matching loop in `luv_engine`. The authoritative pre-trade risk evaluation is strictly `PreTradeRisk` inside `ExecutionGateway`.
