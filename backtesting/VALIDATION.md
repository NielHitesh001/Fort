# Validation performed on 2026-09-27

Host: macOS, AppleClang C++20, Python 3.14. Dependency versions are in
`requirements-lock.txt`; every generated run also records its actual environment.

- Release CTest: `historical_backtest`, `backtest`, `lob`, `execution` passed.
- New replay suite passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- 18 Python unittest cases passed, including end-to-end process-spawn concurrency
  and byte-identical accounting exports for identical runs.
- C++ replay covers symbol isolation, shared liquidity/partial fills, marked
  inventory, net realized P&L, latency, limit/slippage constraints, IOC remainder
  cancellation, cash/position risk, depth walking, deterministic priority,
  callback order deferral, bar sequencing, buffer exhaustion, order-slot reuse,
  invalid events, and repeatability. A global operator-new counter observed zero
  allocations across 100 replay steps with the bundled rolling strategy.
- Python checks duplicate/missing symbols, OHLC/volume validation, timezones,
  sessions/holidays, overlapping bars, causal volume sizing, quote normalization,
  config keys, daily aggregation, drawdowns/recovery duration, initial capital,
  undefined ratios, and partial/open round-trip accounting.
- Offline bar and two-level quote examples ran and exported reports.
- Actual Yahoo downloads for RELIANCE.NS and TCS.NS, 2025-02-03 through 2025-03-31,
  produced 76 bars / 152 events across 38 sessions. Two parameter sets ran
  concurrently against one frozen dataset. Initial capital: INR 1,000,000;
  commission: 3 bps/side; slippage: 2 bps; modeled spread: 2 bps.

| Mean-reversion parameters | Final equity (INR) | Net return | Max drawdown |
|---|---:|---:|---:|
| lookback 10, threshold 1.5%, quantity 20 | 991,236.51 | -0.8763% | 0.9809% |
| lookback 20, threshold 2.5%, quantity 20 | 1,004,047.37 | +0.4047% | 0.2825% |

These are execution-model examples, not evidence of an investable strategy.
Only two completed round trips occurred in each configuration. Vendor snapshots
and reports are local artifacts under `backtesting/runs/` (ignored by Git).

Final NSE batch: `backtesting/runs/backtest-tyy0zsgh/`, with reports in `run-000`
and `run-001`. PDF summary, monthly-return heatmap, and trade overlays were
visually inspected. Each NSE PDF has seven pages; HTML embeds its charts and
includes daily data and the reproducibility manifest.

No full-repository test run, Linux validation, exchange certification, or
large-dataset throughput benchmark is claimed. See README for model limitations.
