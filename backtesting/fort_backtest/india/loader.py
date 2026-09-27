"""Universe selection, pre-period liquidity screens, and existing-engine integration."""

from __future__ import annotations
import json
import math
import pandas as pd
from .archive import IndiaData
from .calendar import Calendar
from .masters import constituents, prefer_exchange
from .transport import DownloadError, sha


def load(config, base):
    if config.get("adjusted", False):
        raise ValueError("bhavcopy is raw/unadjusted; adjusted=True is unsupported")
    if config.get("mode", "bars") != "bars" or config.get("interval", "1d") != "1d":
        raise ValueError("official bhavcopies are daily OHLCV only")
    source = config["source"]
    exchange = {"nse_bhavcopy": "NSE", "bse_bhavcopy": "BSE"}.get(
        source, config.get("exchange", "NSE")
    ).upper()
    if exchange not in ("NSE", "BSE", "BOTH"):
        raise ValueError("exchange must be NSE, BSE, or BOTH")
    if bool(config.get("symbols")) == bool(config.get("universe")):
        raise ValueError("choose exactly one of symbols or universe")
    universe = config.get("universe", "")
    minimum = config.get("min_history_sessions", 504 if universe else 0)
    if type(minimum) is not int or not 0 <= minimum <= 5000:
        raise ValueError("min_history_sessions must be 0..5000")
    coverage = float(config.get("min_coverage", 0.98))
    min_volume = float(config.get("min_avg_volume", 10000 if universe else 0))
    min_turnover = float(config.get("min_avg_turnover", 0))
    jump = float(config.get("price_jump_threshold", 0.35))
    if (
        not 0 < coverage <= 1
        or not 0 < jump < 1
        or any(not math.isfinite(x) or x < 0 for x in [min_volume, min_turnover])
    ):
        raise ValueError("invalid universe/quality filters")
    policy = config.get("quality_policy", "error")
    if policy not in ("error", "warn"):
        raise ValueError("quality_policy must be error or warn")
    start, end = str(config["start"]), str(config["end"])
    a, b = pd.Timestamp(start), pd.Timestamp(end)
    if (
        a >= b
        or a.tz is not None
        or b.tz is not None
        or a != a.normalize()
        or b != b.normalize()
    ):
        raise ValueError("invalid date range")
    screen = (
        minimum > 0
        or min_volume > 0
        or min_turnover > 0
        or config.get("top_n") is not None
    )
    history_start = str(
        config.get(
            "history_start",
            (a - pd.Timedelta(days=max(90, math.ceil(minimum * 1.6) + 60))).date()
            if screen
            else start,
        )
    )
    if pd.Timestamp(history_start) > a:
        raise ValueError("history_start must be <= start")
    if screen and pd.Timestamp(history_start) >= a:
        raise ValueError("liquidity/history screens require data strictly before start")
    store = IndiaData(
        (base / config.get("cache_dir", "../cache/india")).resolve(),
        offline=config.get("offline", False),
    )
    refresh = config.get("refresh", False)
    exchanges = ["NSE", "BSE"] if exchange == "BOTH" else [exchange]
    archives = []
    calendars = {}
    frames = []
    for ex in exchanges:
        cal_exchange = config.get("calendar_exchange", ex).upper()
        calendar_path = config.get("calendar_file")
        cal = Calendar.load(
            store.client,
            history_start,
            end,
            exchange=cal_exchange,
            path=(base / calendar_path).resolve() if calendar_path else None,
            holidays=config.get("holidays", []),
            extra_sessions=config.get("extra_sessions", []),
            refresh=refresh,
        )
        calendars[ex] = cal
        result = store.download_bhavcopy(
            history_start, end, ex, calendar=cal, refresh=refresh
        )
        archives.append(result)
        if result.failures:
            raise DownloadError(
                f"{ex}: {len(result.failures)} daily archives unavailable; see {store.root}/manifests. Resume downloads before backtesting."
            )
        frames.append(store.read_files(result.files))
    raw = pd.concat(frames, ignore_index=True)
    masters = []
    for ex in exchanges:
        if ex == "NSE" or config.get("master_file"):
            master = store.get_symbol_master(
                ex,
                refresh=refresh,
                include_sme=config.get("include_sme", False),
                master_file=(base / config["master_file"]).resolve()
                if config.get("master_file")
                else None,
                max_age_days=config.get("master_max_age_days"),
            )
        else:
            # Scrip codes/ISINs from an official daily file suffice for replay.
            # This is a traded-security snapshot, not a complete listing master.
            f = (
                raw[raw.exchange.eq(ex)]
                .sort_values("date")
                .drop_duplicates(["symbol", "series"], keep="last")
            )
            master = f[["symbol", "exchange", "isin", "name", "series"]].copy()
            master["is_active"] = True
            master["listing_date"] = ""
            master["replay_symbol"] = ""
            master["replay_symbol"] = master.symbol + ".BO"
        masters.append(master)
    master = pd.concat(masters, ignore_index=True)
    series = config.get("series", ["EQ"] if exchange == "NSE" else None)
    master = master[master.is_active.astype(bool)].copy()
    if series:
        if not isinstance(series, list) or not series:
            raise ValueError("series must be a nonempty list")
        master = master[master.series.isin(series)]
    elif exchange == "BOTH":
        master = master[(master.exchange.ne("NSE") | master.series.eq("EQ"))]
    # Filter debt/fund ISINs out of cash-equity universes, preserving old BSE
    # legacy files whose ISIN is unknown. They cannot be deduplicated by ISIN.
    master = master[master["isin"].eq("") | master["isin"].str.startswith("INE")]
    selected_members = None
    unresolved = []
    if universe and universe.upper() != "ALL":
        selected_members = constituents(
            store.client,
            universe,
            refresh=refresh,
            universe_file=(base / config["universe_file"]).resolve()
            if config.get("universe_file")
            else None,
        )
        index_exchange = (
            (
                exchange
                if exchange != "BOTH"
                else config.get("exchange_preference", "NSE")
            )
            if config.get("universe_file")
            else (
                "NSE"
                if universe.upper().replace("_", "").startswith("NIFTY")
                else "BSE"
            )
        )
        if index_exchange not in exchanges:
            raise ValueError("universe does not match source exchange")
        members = set(selected_members.symbol)
        candidates = master[
            master.exchange.eq(index_exchange) & master.symbol.isin(members)
        ]
        missing = members - set(candidates.symbol)
        unresolved = [
            {
                "symbol": s,
                "reasons": ["constituent absent from eligible active master/series"],
            }
            for s in sorted(missing)
        ]
        if exchange == "BOTH":
            isins = set(candidates["isin"]) - {""}
            candidates = master[
                master.index.isin(candidates.index) | master["isin"].isin(isins)
            ]
    elif universe:
        candidates = master
    else:
        requested = config["symbols"]
        if (
            not isinstance(requested, list)
            or not requested
            or not all(isinstance(s, str) and s for s in requested)
            or len(set(requested)) != len(requested)
        ):
            raise ValueError("symbols must be unique strings")
        picks = []
        for symbol in requested:
            bare = symbol.removesuffix(".NS").removesuffix(".BO")
            found = master[
                master.symbol.eq(bare)
                | master.yahoo_ticker.eq(symbol)
                | master.replay_symbol.eq(symbol)
            ]
            if symbol.endswith(".NS"):
                found = found[found.exchange.eq("NSE")]
            if symbol.endswith(".BO"):
                found = found[found.exchange.eq("BSE")]
            if found.empty:
                raise ValueError(
                    f"symbol {symbol} missing from master; use NSE ticker or BSE numeric code"
                )
            picks.append(found)
        candidates = pd.concat(picks).drop_duplicates(["exchange", "symbol", "series"])
        if exchange == "BOTH":
            isins = set(candidates["isin"]) - {""}
            candidates = master[
                master.index.isin(candidates.index) | master["isin"].isin(isins)
            ]
    candidates = prefer_exchange(
        candidates, config.get("exchange_preference", "NSE")
    ).sort_values(["exchange", "symbol"])
    # Resolve renames through ISIN as well as current ticker; never merge rows
    # across exchange or share series. New/old ISINs are not guessed.
    retained = []
    excluded = list(unresolved)
    panels = []
    profiles = []
    for row in candidates.to_dict("records"):
        f = raw[
            raw.exchange.eq(row["exchange"])
            & (
                raw.series.isin(series)
                if series
                else (
                    raw.series.ne("")
                    if row["exchange"] == "BSE"
                    else raw.series.eq(row["series"])
                )
            )
            & (
                raw.symbol.eq(row["symbol"])
                | (raw["isin"].eq(row["isin"]) if row["isin"] else False)
            )
        ].copy()
        f["symbol"] = row["replay_symbol"]
        if f.date.duplicated().any():
            raise ValueError(f"ambiguous rename/series for {row['symbol']}")
        f = f.sort_values("date")
        before = f[f.date.lt(start)]
        expected_pre = (
            calendars[row["exchange"]].sessions(history_start, start)
            if pd.Timestamp(history_start) < a
            else []
        )
        active = before[
            (pd.to_numeric(before.volume) > 0) & before.quality_issue.eq("")
        ]
        avg_volume = float(pd.to_numeric(active.volume).mean()) if len(active) else 0.0
        turnover = pd.to_numeric(active.turnover, errors="coerce")
        avg_turnover = (
            float(turnover.mean()) if len(active) and turnover.notna().all() else None
        )
        reason = []
        if len(active) < minimum:
            reason.append(f"history {len(active)} < {minimum}")
        if screen and expected_pre and len(active) / len(expected_pre) < coverage:
            reason.append("insufficient pre-period coverage")
        if avg_volume < min_volume:
            reason.append("low pre-period volume")
        if min_turnover and (avg_turnover is None or avg_turnover < min_turnover):
            reason.append("missing or low pre-period turnover")
        if config.get("top_n") is not None and avg_turnover is None:
            reason.append("turnover unavailable for ranking")
        if config.get("min_market_cap") is not None:
            threshold = float(config["min_market_cap"])
            if not math.isfinite(threshold) or threshold < 0:
                raise ValueError("invalid min_market_cap")
            cap_date = pd.to_datetime(row.get("market_cap_asof", ""), errors="coerce")
            if (
                not row.get("market_cap_inr")
                or pd.isna(cap_date)
                or cap_date.tz is not None
                or cap_date >= a
            ):
                raise ValueError(
                    "market-cap filter needs master_file market_cap_inr and market_cap_asof strictly before start; bhavcopy does not provide shares outstanding"
                )
            cap = float(row["market_cap_inr"])
            if not math.isfinite(cap) or cap < 0:
                raise ValueError("invalid market cap")
            if cap < threshold:
                reason.append("below minimum market cap")
        if reason:
            excluded.append({"symbol": row["replay_symbol"], "reasons": reason})
            continue
        retained.append(row)
        panels.append(f)
        profiles.append(
            {
                "symbol": row["replay_symbol"],
                "history_sessions": len(active),
                "avg_volume": avg_volume,
                "avg_turnover": avg_turnover,
            }
        )
    if not retained:
        raise ValueError(
            "no symbols pass pre-period history/liquidity filters; adjust the documented filters explicitly"
        )
    if config.get("top_n") is not None:
        n = config["top_n"]
        if type(n) is not int or not 1 <= n <= 512:
            raise ValueError("top_n must be 1..512")
        keep = {
            p["symbol"]
            for p in sorted(profiles, key=lambda p: (-p["avg_turnover"], p["symbol"]))[
                :n
            ]
        }
        excluded.extend(
            {"symbol": r["replay_symbol"], "reasons": ["outside top_n turnover rank"]}
            for r in retained
            if r["replay_symbol"] not in keep
        )
        retained = [r for r in retained if r["replay_symbol"] in keep]
        panels = [f for f in panels if f.symbol.iloc[0] in keep]
    symbols = sorted(r["replay_symbol"] for r in retained)
    if len(symbols) > 512:
        raise ValueError(
            "universe exceeds 512 replay symbols; supply top_n or explicit filters"
        )
    issues = []
    output = []
    for f in panels:
        s = f.symbol.iloc[0]
        ex = f.exchange.iloc[0]
        period = f[f.date.ge(start) & f.date.lt(end)].copy()
        expected = {str(d) for d in calendars[ex].sessions(start, end)}
        missing = sorted(expected - set(period.date))
        zeros = period.loc[pd.to_numeric(period.volume).eq(0), "date"].tolist()
        jumps = f.loc[
            pd.to_numeric(f.close).pct_change().abs().gt(jump), "date"
        ].tolist()
        jumps = [d for d in jumps if start <= d < end]
        invalid = period.loc[period.quality_issue.ne(""), "date"].tolist()
        for kind, dates in [
            ("missing_sessions", missing),
            ("zero_volume", zeros),
            ("possible_corporate_action_or_bad_price", jumps),
            ("invalid_ohlc", invalid),
        ]:
            if dates:
                issues.append({"symbol": s, "kind": kind, "dates": dates})
        if invalid:
            raise ValueError(
                f"{s}: invalid OHLC in selected archive rows: {invalid[:5]}"
            )
        if period.empty:
            raise ValueError(f"{s}: no observations in backtest period")
        period = period.rename(columns={"date": "timestamp"})
        output.append(period)
    if issues and policy == "error":
        raise ValueError(
            f"raw-data quality checks failed: {json.dumps(issues)[:2000]}; inspect corporate actions/gaps, or explicitly select quality_policy: warn"
        )
    combined = (
        pd.concat(output)
        .sort_values(["timestamp", "symbol"], kind="stable")
        .reset_index(drop=True)
    )
    snapshot = pd.DataFrame(retained).to_csv(index=False).encode()
    metadata = {
        "source": "official NSE/BSE bhavcopy",
        "label": "Official unadjusted Indian cash-equity bhavcopy",
        "adjustment": "raw/unadjusted; jumps are heuristics, not a corporate-action ledger",
        "master_sha256": sha(snapshot),
        "resolved_master": retained,
        "constituents_sha256": sha(selected_members.to_csv(index=False).encode())
        if selected_members is not None
        else None,
        "universe": universe,
        "membership": "frozen current constituents/master; historical survivorship bias unless a dated universe_file is supplied",
        "filters_use": "strictly before backtest start",
        "profiles": profiles,
        "excluded": excluded,
        "quality_issues": issues,
        "calendars": {
            ex: {
                "sources": cal.sources,
                "holidays": sorted(cal.holidays),
                "extra_sessions": sorted(cal.extra_sessions),
            }
            for ex, cal in calendars.items()
        },
        "calendar_proxy": config.get("calendar_exchange"),
        "archives": [p for r in archives for p in r.provenance],
        "currency": "INR",
        "symbols": symbols,
    }
    # Canonical symbols propagate to the runner, labels, and saved resolved config.
    config.pop("universe", None)
    config["symbols"] = symbols
    config["holidays"] = sorted(
        set.intersection(*(c.holidays for c in calendars.values()))
    )
    config["extra_sessions"] = sorted(
        set.union(*(c.extra_sessions for c in calendars.values()))
    )
    config["interval"] = "1d"
    config["mode"] = "bars"
    return combined, metadata
