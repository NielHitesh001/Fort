# Official Indian-market EOD data

The primary Indian equity sources are now official NSE/BSE daily archives.
Yahoo remains an explicitly secondary convenience source; an official-source
failure never falls back to Yahoo or synthetic data automatically.

## Quick start

Run from the repository root after the setup in [README](README.md):

```sh
# Full current Nifty 50 snapshot; short, deliberately relaxed smoke example.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/nifty50_official.yaml

# Larger index with the same smoke settings, without changing strategy code.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/nifty50_official.yaml --universe nifty500 --source nse_bhavcopy

# Replay the pinned local master, constituent list, calendar and archives offline.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/nifty50_official.yaml --offline

# BSE Sensex, with an explicitly selected NSE cash-calendar proxy.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/bse_official.yaml

# Pre-start two-year history/coverage/liquidity screens, stricter data quality.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest backtesting/examples/nifty500_official.yaml
```

The smoke configs explicitly set `min_history_sessions: 0`,
`min_avg_volume: 0`, and `quality_policy: warn` so a short period can exercise the
whole available liquid-index universe. These are testing settings, not a
recommendation to ignore research data problems. The stricter Nifty 500 example
can exclude recent listings or fail on unresolved corporate-action/gap warnings.

The replay engine supports 512 symbols and 2,048 simultaneously active orders,
including a 500-symbol allocation-free test. Bar volume supports up to one
trillion shares; order quantities and each modeled depth level remain bounded
at one billion. Reports default to at most 12 per-symbol charts to keep a
500-stock report usable. `analytics.max_trade_charts` controls this limit;
CSV exports always contain all symbols and fills.

## Files and interfaces

```
backtesting/fort_backtest/india/
    transport.py   # bounded HTTPS, global cache rate limit, retries, atomic files
    parsers.py     # legacy NSE, legacy BSE, UDiFF, NSE index close files
    calendar.py    # cached annual trading calendars and explicit overrides
    masters.py     # symbol masters, constituents and ISIN exchange preference
    archive.py     # daily collection, processed files, Python public API
    loader.py      # pre-period selection, quality checks, backtester adapter
    __main__.py    # data CLI; independent of replay and report generation
```

```python
from fort_backtest.india import IndiaData

india = IndiaData("backtesting/cache/india")
master = india.get_symbol_master("NSE", include_sme=True)
result = india.download_bhavcopy("2024-01-01", "2026-01-01", "NSE")
print(result.failures)  # explicit failures; the collector continues other days

bars = india.load_ohlcv("RELIANCE", "2024-01-01", "2026-01-01", adjusted=False)
# DatetimeIndex named date; OHLCV + exchange, series, ISIN, optional turnover,
# deliverable_qty and quality flags. bars.attrs includes raw-data warnings.
```

Module-level `get_symbol_master`, `download_bhavcopy`, and `load_ohlcv` wrappers
accept the same arguments plus `root=` and `offline=`. Dates use an **exclusive
end**, consistently with the existing backtester. `adjusted=True` is rejected;
no implicit synthetic adjustment is made. Prices/turnover are INR and volume is
shares. There is no FX conversion. Daily bars enter replay in Asia/Kolkata using
the existing 09:15–15:30 session model; special evening sessions are excluded
unless the user explicitly elects to include them as EOD observations.

## Collect and refresh independently

```sh
# These global options come before the subcommand.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india --cache backtesting/cache/india download --start 2024-01-01 --end 2026-01-01 --exchange NSE

# Update mainboard plus SME masters; retains versioned normalized snapshots.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india --refresh master --exchange NSE --include-sme

# Safe to invoke periodically: checks the master age and refreshes after 7 days.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india master --exchange NSE --max-age-days 7

# Freeze or deliberately refresh a constituent snapshot.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india universe NIFTY500
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india --refresh universe NIFTY500

# Official index OHLC/close measures for benchmark analysis (not equities).
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india download --start 2025-03-28 --end 2025-03-29 --kind indices

# Export one symbol from cache in the existing timestamp-based CSV schema.
PYTHONPATH=backtesting backtesting/.venv/bin/python -m fort_backtest.india --offline load RELIANCE --start 2024-01-01 --end 2026-01-01 --output backtesting/runs/reliance.csv
```

`load` also writes a `.metadata.json` sidecar so raw-price warnings survive CSV
export. `download` returns nonzero if any expected session failed, even though
successfully downloaded days are retained. A second invocation resumes cached
work. `--refresh` retries known missing files explicitly but **never bypasses**
server cooldowns. Routine master/universe refresh is separate from historical
archive refresh. Config `master_max_age_days` enables age-based master refresh;
otherwise cached masters remain pinned until explicitly refreshed.

No scheduler was installed. The master-refresh or date-range download CLI can
be invoked by an existing scheduler after data publication. This implementation
uses one request at a time across collectors sharing a cache, not parallel
exchange downloads.

## Storage and network behavior

Default layout (cache directory is configurable):

```
backtesting/cache/india/
    raw/nse/equity/YYYY/MM/<official ZIP or CSV>
    raw/bse/equity/YYYY/MM/<official ZIP or CSV>
    raw/nse/indices/YYYY/MM/<index CSV>
    processed/nse/equity/<date>-<raw-sha256>.csv
    processed/bse/equity/<date>-<raw-sha256>.csv
    masters/raw/...
    masters/<exchange>-<snapshot-hash>.csv
    masters/universes/<universe>.<csv|json>
    masters/universes/<universe>-<snapshot-hash>.csv
    calendars/nse-<year>.json
    manifests/<exchange>-<kind>-<start>-<end>.json
    downloads.jsonl
    cache/<locks, rate-limit state>
```

Raw file sidecars record URL, SHA-256 and acquisition time. Processed files are
keyed by raw input hash, so a concurrent refresh cannot change the data underlying
an already selected file. Per-resource and cache-wide `flock` locks coordinate
threads/processes on macOS/Linux. Files are committed through atomic replacement
only after validation; no partial download is published as valid data. ZIPs are
read in memory without extracting paths, with a single CSV member and bounded
compressed/uncompressed sizes.

The default request interval is 1.05 seconds plus jitter, across the entire
shared cache. Three bounded attempts handle network failures and 5xx responses
with exponential backoff. `Retry-After` is honored. HTTP 403/429 establishes a
persistent host cooldown; the client does not cycle identities, cookies, hosts,
or user agents to evade it. HTTP 404/410 gets a 12-hour negative cache and is
recorded as **missing data, not automatically a holiday**. All attempts, HTTP
statuses, cache hits and failures go to `downloads.jsonl`; `--verbose` also logs
them to stderr. Separate cache roots do not coordinate their rate budgets: use
one shared root for concurrent collectors.

Offline mode never contacts an exchange and accepts only verified cached input.
It does not silently use corrupt files or fetch missing prerequisites. Cache the
calendar, masters, constituents and desired archive dates before disconnecting.

## Universe and identity rules

Supported names: `NIFTY50`, `NIFTYNEXT50`, `NIFTY100`, `NIFTY200`, `NIFTY500`,
`SENSEX`, `BSE100`, `BSE500`, and `ALL`. Index lists occasionally contain more
than their nominal stock count or temporary corporate-action placeholders; the
loader uses actual published members, not a hard-coded company list.

Choose exactly one of `symbols` or `universe`. Examples:

```yaml
data:
  source: nse_bhavcopy
  symbols: [RELIANCE, TCS, INFY]  # .NS suffixes also accepted
  start: '2025-03-03'
  end: '2025-04-01'
  cache_dir: ../cache/india
```

For BSE, use the six-digit **scrip code**, such as `500325`, or the replay ID
`500325.BO`. The code is stable across display-name changes. The replay ID is not
a promise that Yahoo recognizes the same ticker. The master contains the
requested fields `symbol, exchange, isin, name, series, is_active, listing_date,
yahoo_ticker`, plus `replay_symbol`. NSE Yahoo mappings are constructed as `.NS`;
BSE Yahoo mappings remain blank when unknown. A normalized imported master may
supply a verified mapping. Unknown BSE listing dates remain blank.

The BSE listing API can deny automated access: this environment returned 403.
`master --exchange BSE --master-file /path/to/official-list.csv` imports an
exchange-exported List of Securities or a normalized master. BSE archive replay
can also construct a **traded-security snapshot** from cached bhavcopies, which
is sufficient for numeric scrip-code or index selection; it is not represented
as the complete BSE listing register. NSE supports official mainboard and SME
master CSVs. `include_sme: true` includes SME records; also select suitable series
(e.g. `series: [EQ, SM, ST]`) when choosing them.

`source: india_bhavcopy` with `exchange: BOTH` enables a combined pool. Matching
ISINs are deduplicated using `exchange_preference: NSE` by default; set `BSE` to
prefer BSE. Preference applies to dual-listed identity, even if both listings
were requested. Blank ISINs never merge unrelated securities. This selects one
listing for the entire run; it does not switch venues or splice daily prices
when one is missing. Rename matching uses the same ISIN within the same exchange
and allowed series, rejecting ambiguous daily matches. It does not guess across
split-related ISIN changes. NSE selection defaults to EQ; use `series: [EQ, BE]`
explicitly if trade-to-trade history should be eligible.

Index members absent from the eligible master/series, plus those failing
screens, are listed in manifest/report `excluded` with reasons. Explicit unknown
symbols fail. `universe_file` accepts a frozen CSV with a `symbol` column for
custom/historical membership. The user must establish its as-of validity.
**Current** masters and constituent lists do not supply historical memberships:
using them for old periods has survivorship bias, explicitly disclosed in reports.
The resolved master, selected symbols, constituent hash, calendar dates, raw and
processed hashes are recorded with each run. Selection and replay are deterministic
for the same frozen snapshots. A deliberate refresh can change results.

## History, liquidity and market-cap filters

Named universes default to 504 valid, positive-volume pre-period sessions,
98% pre-period coverage, and average volume of 10,000 shares. Explicit symbol
requests default to no history/liquidity screen. `history_start` supplies the
screening window; otherwise a conservative lookback is derived. All screens
use data **strictly before `start`**, never backtest-period volume or prices.

Available controls:

- `min_history_sessions`, `min_coverage`, `min_avg_volume`.
- `min_avg_turnover`: INR; averages use valid positive-volume sessions.
- `top_n`: choose highest pre-period average turnover; ties break by symbol.
- `min_market_cap`: requires an imported master with `market_cap_inr` and
  `market_cap_asof` strictly before the start. Missing/future/nonfinite values
  fail; market cap cannot be reconstructed from bhavcopy without shares outstanding.

The master is still a static selection snapshot: pre-period liquidity screens
prevent future-volume leakage, but do not cure current-membership survivorship
bias. More than 512 selected symbols requires an explicit filter or smaller
universe; the engine never silently truncates its symbol list.

## Calendars and quality

NSE annual holiday JSON is fetched from the public trading-holiday endpoint,
validated to contain the requested year, and cached. Unsupported/wrong-year
responses fail rather than treating weekdays as known sessions. Weekends and
known holidays are skipped; explicit `extra_sessions` overrides them for special
sessions. Muhurat/evening sessions are excluded by the normal-session calendar
unless opted in; the replay's bar timing model remains the regular session.

BSE requires either a verified `calendar_file` or the explicit
`calendar_exchange: NSE` proxy used in the example. The proxy choice is recorded;
NSE and BSE calendars are not silently assumed identical. For historical years
whose public calendar endpoint is unavailable, import a verified calendar:

```json
{
  "exchange": "BSE",
  "years": [2025],
  "holidays": ["2025-02-26", "2025-03-14", "2025-03-31"],
  "extra_sessions": ["2025-02-01"]
}
```

The abbreviated snippet shows the schema, **not a complete calendar**. Supply the
full official holiday list for every declared year. On multi-exchange runs a
shared calendar requires an explicit proxy; separately collected exchange data
can instead be supplied through the existing CSV path.

Archive validation checks schemas, dates (legacy BSE relies on its dated URL),
exchange/segment, duplicate symbol-series records, finite numeric fields and
integer nonnegative volumes. Malformed OHLC rows are retained with a flag in the
processed archive so an unrelated T+0/security row cannot block all EQ stocks;
selecting such a row for replay always fails. The backtesting adapter additionally
checks missing expected dates, zero volume, and absolute close-to-close jumps
above `price_jump_threshold` (default 35%). `quality_policy: error` is the default;
`warn` retains valid observed bars and records every warning in the manifest/HTML.
It never fills missing dates or prices. A missing entire daily archive always
blocks a backtest, even with `warn`; resume the collector first.

**Bhavcopy prices are unadjusted.** Jump flags are only a heuristic: a small bonus,
dividend, rights issue, or security change may not trigger them. There is no
corporate-action cash/share ledger. Total-return interpretation is invalid around
unhandled actions. Yahoo's explicitly adjusted prices can be studied separately
through documented CSV experiments; no Yahoo data is mixed into official series.
NSE index files include price indices and other measures (including zero dividend
points); they are exported for benchmark analysis and are not tradable securities.

## Official references and use

- [NSE daily/historical reports](https://www.nseindia.com/all-reports): legacy CM
  bhavcopy was discontinued from July 8, 2024 in favor of UDiFF.
- [NSE mainboard master](https://nsearchives.nseindia.com/content/equities/EQUITY_L.csv)
  and [SME master](https://nsearchives.nseindia.com/emerge/corporates/content/SME_EQUITY_L.csv).
- [Nifty 500 constituents](https://www.niftyindices.com/IndexConstituent/ind_nifty500list.csv).
- [BSE bhavcopy](https://www.bseindia.com/markets/MarketInfo/BhavCopy.aspx) and
  [BSE 500 index](https://www.bseindices.com/indices-details/code/17/).
- [NSE annual trading-calendar endpoint](https://www.nseindia.com/api/holiday-master?type=trading&year=2025).

Use public downloads for personal/research work subject to the exchanges' terms.
Public availability does not grant redistribution rights; do not redistribute
bulk data commercially without appropriate licensing. No data subscription is
required by this implementation. Licensed exchange or vendor feeds are optional
future upgrades for point-in-time universes, corporate actions, historical depth,
and contractual availability guarantees. No paid dependency or authentication
bypass has been added.
