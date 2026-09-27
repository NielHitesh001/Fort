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

## Official-data expansion — 2026-09-27

The official NSE/BSE extension was exercised against real public downloads as
well as isolated synthetic-schema fixtures. No bulk market data is committed.

- 54 Python tests passed, including parser transitions, rejected/truncated
  downloads, 403/429/503 cooldowns and Retry-After, negative caching, offline
  integrity, resumability, holidays, masters, ISIN preference, pre-period
  filtering, date-valid market-cap inputs, raw-price warnings, and CLI integration.
- Release CTest: `historical_backtest`, `backtest`, `lob`, `execution` passed.
- Historical suite passed under AddressSanitizer/UndefinedBehaviorSanitizer.
  New checks execute 500 simultaneous symbol orders with zero observed replay
  allocations and accept daily bar volume exceeding one billion shares.
- All five Nifty constituent lists fetched successfully (50, 50, 100, 200, 501
  entries respectively). Official BSE lists returned 30 Sensex, 100 BSE 100, and
  502 BSE 500 entries. Counts are the observed snapshots, not hard-coded limits.
- NSE mainboard and SME master download formats were verified. BSE's listing API
  returned 403; no bypass was attempted. Import support and traded-security
  snapshots provide the documented alternatives.
- Two years of NSE archives, 2024-01-01 through 2025-12-31, completed with **493
  expected sessions, 493 successful files, zero failures**, spanning legacy and
  UDiFF formats. All three major symbols RELIANCE, TCS and SBIN subsequently
  loaded 493 observations entirely offline, with no missing or zero-volume days.
  RELIANCE's 2024-10-28 discontinuity was flagged as a possible corporate action.
- Actual BSE files for 2024-07-05 and 2024-07-08 validated both sides of the format
  transition. The NSE index file for 2025-03-28 yielded 137 benchmark records.
- Current-constituent Nifty 50 (all 50 names), Sensex (all 30 names), and a larger
  Nifty 500 selection ran end to end. The last selection retained 499 stocks;
  `DUMMYHEG` and `HFCL` were excluded because they were absent from the eligible
  active-master/EQ-series set. Exclusions are recorded, not silently truncated.
- Repeated offline Nifty 50, Sensex and 499-symbol runs produced byte-identical
  trade, equity, position, daily P&L and metric files. Their source snapshots and
  manifests retain hashes and resolved selections.

Example local artifacts:

| Run | Local report directory | Symbols | Sessions | Fills |
|---|---|---:|---:|---:|
| Nifty 50, August 2026 | `runs/backtest-xl09m7ag/run-000` | 50 | 21 | 94 |
| Sensex, March 2025 | `runs/backtest-4icr0l17/run-000` | 30 | 19 | 66 |
| Nifty 500 eligible selection, August 2026 | `runs/backtest-cnzrmddp/run-000` | 499 | 21 | 1,056 |

The 499-symbol PDF has 11 pages; its summary was rendered and checked visually.
The report records two exclusions and zero detected quality-issue records for
that short period. These smoke runs explicitly relax pre-period history/liquidity
screens; they do not validate historical index membership, total-return treatment
of corporate actions, or strategy profitability. Sensex uses the explicitly
configured NSE cash-calendar proxy, not an independently certified BSE calendar.

The two-year collection summary and per-symbol checks are stored locally as
`cache/india/multi-year-validation.json` and
`cache/india/multi-year-symbol-validation.json`. See [India data guide](INDIA_DATA.md)
for repeatable commands, assumptions, limitations and official references.
