# Historical backtesting for Indian cash equities

Fort now has a separate historical research pipeline: validated Python ingestion →
normalized event file → allocation-free C++ replay → Python analytics and reports.
The legacy `luv_backtest.hpp` stub remains available for compatibility; this
pipeline uses `luv_historical_backtest.hpp` and does not use its cash-flow metrics.

## Official NSE/BSE coverage

See [India data guide](INDIA_DATA.md) for official bhavcopy downloads, Nifty/Sensex/BSE universes, pre-period history/liquidity filters, offline replay, and master/calendar refresh. Official archives are the primary path; Yahoo remains a secondary convenience source.

## Run

From the repository root (Python 3.11+ and a C++20 compiler):

```sh
python3 -m venv backtesting/.venv
backtesting/.venv/bin/pip install -r backtesting/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target fort_replay luv_historical_backtest -j 2

# Offline, explicitly synthetic two-symbol fixture
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/demo.json

# Offline quote-level momentum example with two levels of depth
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/quotes.json

# Actual NSE data from Yahoo; two parameter runs in separate worker processes
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/nse.yaml

ctest --test-dir build -R '^historical_backtest$' --output-on-failure
PYTHONPATH=backtesting backtesting/.venv/bin/python -m unittest discover -s backtesting/tests -v
```

Use `--binary /path/to/fort_replay` for a different build and `--output /path` to
choose the output parent. Paths in config are relative to the config file.
Each invocation reserves a new directory; it never overwrites another run.
`SUCCESS` is written only after replay and reporting finish. A failed run is
nonzero and may leave diagnostic files, but must not be treated as a result.

The YAML example downloads RELIANCE.NS and TCS.NS from 2025-02-03 through
2025-03-31. `start` is inclusive; `end` is exclusive. `.NS` selects NSE and `.BO`
selects BSE. The same adapter supports intraday OHLCV within the vendor's
retention limits. Network errors and missing symbols fail rather than silently
substitute synthetic prices. It downloads once per batch, freezes the input,
and gives every parameter run the same events.

## Execution and accounting contract

- **Scope:** long-only, fully funded cash equities, at most 512 symbols per run.
  Market orders are IOC on the next eligible executable event; limit orders
  persist until filled or cancelled. There is no forced final liquidation.
  Outstanding orders and positions are exported at the end.
- **Bar causality:** open events expose only synthetic bid/ask at the open.
  Completed OHLCV is delivered at bar end minus one nanosecond, followed by the
  next open. Orders cannot fill at the close that generated their signal.
  High/low touches never imply fills. Bar timestamps denote starts; daily bars
  cover 09:15–15:30 IST. Incomplete intraday bars are clipped at session close.
- **Bar capacity:** each side gets `floor(previous_bar_volume * participation)`
  shares. The first bar has zero capacity. Spread is a configurable modeled
  spread around the open. This is a research approximation, not a claim about
  available opening-auction or continuous-market liquidity.
- **Quote causality:** each CSV row is a complete independent quote snapshot
  with up to five price levels. Existing eligible orders consume its shared
  depth before the strategy callback. Orders submitted at the same timestamp
  wait for a strictly later snapshot, even across symbols. Snapshots explicitly
  replenish capacity; repeated snapshots may overestimate liquidity. These are
  not exchange message deltas or inferred queue-position fills.
- **Priority:** buys are processed before sells; within each side, market first,
  then aggressive limit price, then order ID (submission order). Levels are
  consumed best first, shared by every eligible order. Latency is a minimum
  delay, checked against event timestamps. Cancels take effect immediately.
- **Costs:** prices are integer paise; configured adverse slippage is rounded up
  to the next paise. A slipped price can never violate a limit. Commission is
  basis points of filled notional on both sides. It is an aggregate configurable
  research cost, not a built-in broker/tax/STT schedule. Exchange tick-size
  rules and per-order brokerage caps are not modeled.
- **Risk:** cash, long inventory, and maximum position are checked at execution;
  pending orders do not reserve cash. Unaffordable quantities partially fill;
  orders with no affordable/owned quantity are rejected and removed. Use return
  values and `rejections()` to observe admission and execution risk failures.
- **P&L:** equity = cash + marked inventory; cash outflow is not a trading loss.
  Weighted-average cost basis includes opening fees; realized closing P&L
  subtracts allocated basis and closing fees. Slippage is already in prices.
  Do not subtract displayed slippage a second time. Unfinished round trips are
  excluded from win/loss statistics but remain in marked equity.

## Data contract and validation

CSV bars:

```csv
timestamp,symbol,open,high,low,close,volume
2025-02-03,RELIANCE.NS,1260,1270,1240,1250,1000000
```

CSV quotes (`mode: quotes`):

```csv
timestamp,symbol,bid,ask,bid_qty,ask_qty
2025-02-03T09:15:00+05:30,RELIANCE.NS,1250.00,1250.05,100,120
```

Optional quote columns `bid1,bid_qty1,ask1,ask_qty1` through level `4` must be
contiguous and strictly sorted. Trade-only ticks are rejected: they cannot
identify executable two-sided liquidity. The runner's normalized CSV uses UTC
nanoseconds, numeric symbol indices, and paise (see `data.COLUMNS`). Keep raw
input prices in INR.

Naive timestamps mean IST; aware timestamps are converted to IST. Intraday data
outside 09:15–15:30, duplicate symbol timestamps, overlapping bars, missing
requested symbols, crossed quotes, nonfinite/nonpositive prices, fractional or
negative volume, and invalid OHLC envelopes are rejected. Weekend records fail
unless listed in `extra_sessions`; `holidays` explicitly excludes dates. Both
lists use quoted `YYYY-MM-DD` strings. These CSV/Yahoo calendar settings do not constitute a maintained NSE/BSE
holiday database; missing sessions are neither filled nor assumed to be holidays.
The official-data path uses cached annual NSE calendars and an explicit BSE calendar or proxy.

Yahoo is called with `auto_adjust=False`. Any fetched dividend/split/capital-gain
action causes the run to fail because the engine has no corporate-action ledger.
Choose an action-free period. A user-supplied adjusted CSV can be used for a
synthetic adjusted-price experiment, but must be labeled as such; it is not a
simulation of historical share counts. CSV users are responsible for checking
corporate actions and data licensing. Free OHLCV does not provide exchange order
queues or a survivorship-bias-free security universe.

Primary references: [yfinance API](https://ranaroussi.github.io/yfinance/reference/yfinance.functions.html)
and [NSE cash-equity market timings](https://www.nseindia.com/static/market-data/market-timings).

## Strategy interface

Implement `luv::historical::Strategy` in C++. Every method is `noexcept` and must
avoid allocation. Instantiate strategy state before replay. The bundled
`RollingStrategy` supports `mean_reversion` and `momentum`, compares against the
previous N observations, and avoids duplicate orders while a symbol is pending.

```cpp
struct Example final : luv::historical::Strategy {
    void on_market(const luv::historical::Event& event,
                   luv::historical::Engine& engine) noexcept override {
        using namespace luv::historical;
        const auto& position = engine.position(event.symbol);
        const auto& depth = engine.book(event.symbol);
        // book_timestamp() identifies stale depth between bar opens.
        if (!position.qty && !engine.pending(event.symbol) && depth.asks[0].price)
            engine.submit(event.symbol, Side::Buy, 1); // next-event market IOC
    }
    void on_fill(const luv::historical::Fill&, luv::historical::Engine&) noexcept override {}
};
```

`submit(symbol, side, quantity, limit_paise=0)` returns an order ID or zero on
rejection. `cancel(id)` returns whether an active order was cancelled. Strategies
can query `position`, `book`, `book_timestamp`, `cash`, `equity`, and `pending`.
Fill callbacks run after all matching for the event, before the market callback;
new callback orders never execute against the same snapshot. Custom C++ strategies
can use the header directly or be added to the runner factory in `replay.cpp`.
Python runs ingestion/reporting only; there is no Python callback in replay.

Fort's `LOBEngine` reconstructs historical ITCH orders; it is not a general
counterfactual matching venue. `luv_historical_lob_adapter.hpp::from_lob` copies
five levels directly from its Arena on the owning thread. It divides Fort's
1/10000 prices into paise by default, rejecting nonrepresentable prices. Adjust
the divisor only for a known feed scale. Feed the returned quote into `step`;
the source LOB is not mutated by simulated strategy executions. A live NSE/BSE
feed decoder and historical order-queue simulation are outside this adapter.

## Memory, concurrency, reproducibility

The engine allocates fill and sample buffers once at construction. Orders,
positions, depth, and rolling windows use fixed arrays; replay does not resize,
allocate, perform I/O, or read a wall clock. Defaults: 2,048 simultaneous active
orders, five levels, 100,000 fills; equity capacity equals the loaded event count.
Inactive order slots are reused. Sample/fill exhaustion stops the run explicitly.
This preserves Fort's preallocation pattern without allocating its large global
Arena for every OHLCV run. The adapter can use an existing Arena when available.

Each worker owns its engine, strategy, input state, and output directory. Config
`workers` controls independent processes; `runs` is a list of strategy overrides.
The fixed `seed` is recorded and seeded for extensions; matching itself uses no
randomness. Events sort by timestamp, symbol, then kind. Identical input and
binary yield identical fills/equity; floating-point accounting can differ across
compiler/architecture combinations. Replay guarantees do not extend to future
vendor revisions: reuse the saved `source.csv` or `events.csv`.

Each run's manifest records resolved config, dependencies, seed, symbol mapping,
source and event SHA-256, binary SHA-256, and engine counters. The batch saves
`source.csv`, `events.csv`, and `comparison.json`. For exact reconstruction, keep
those alongside the executable and environment. `requirements-lock.txt` captures
the environment validated during development; the ranges in `requirements.txt`
are intended for portable installation.

## Reports and metric definitions

Each run exports:

- `trades.csv`: every fill with timestamp, symbol index, price, costs, closing
  quantity, and realized P&L; `round_trips.csv`: completed flat-to-flat episodes.
- `positions.csv`: final held/pending symbols; `equity.csv`: event samples;
  `daily_pnl.csv`: last observed sample per IST date, daily P&L and returns.
- `metrics.json`, resolved `config.json`, and `manifest.json`.
- Self-contained `report.html`, multipage `report.pdf`, and PNG/SVG charts for
  equity, drawdown, monthly/yearly returns, daily return distribution, and fills
  over each symbol's price history.

Total return uses initial capital. CAGR uses inclusive elapsed calendar days
and is undefined for one observed session. Daily Sharpe uses sample standard
deviation of excess returns and sqrt(periods_per_year). Sortino uses the root
mean square of negative excess returns over **all** observed sessions. Annual
risk-free rate is converted geometrically. Undefined ratios are JSON null / N/A.
Drawdown includes initial capital and every event, including unrecovered spans;
duration is elapsed calendar time from peak to recovery/end. Monthly and yearly
returns compound daily returns; partial periods are labeled. Win rate counts
profitable completed round trips; breakevens are in its denominator, and average
loss is a negative INR value. Displayed execution costs are direct cost totals,
not the result of a separate zero-cost counterfactual simulation.

## Readiness

This delivers a runnable, tested research backtester, not a certification of
production readiness. Before institutional deployment it still needs point-in-time
security masters and exchange-calendar exception verification, corporate-action and delisting handling,
point-in-time universe data, realistic impact/queue models where required,
market-specific tax schedules, and workload-specific performance/operational
validation. These limitations are surfaced in the reports rather than hidden
behind apparently precise performance statistics.
