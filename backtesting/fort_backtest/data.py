"""Validate vendor data and normalize it into causal, UTC-nanosecond events."""
from __future__ import annotations
import hashlib
from pathlib import Path
import numpy as np
import pandas as pd

TZ = "Asia/Kolkata"
INTERVALS = {"1m": 1, "2m": 2, "5m": 5, "15m": 15, "30m": 30, "60m": 60, "90m": 90, "1h": 60, "1d": 375}
COLUMNS = ["timestamp", "symbol", "kind", "open", "high", "low", "close", "volume"] + [f"{name}{i}" for i in range(5) for name in ("bid", "bid_qty", "ask", "ask_qty")]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(config: dict, base: Path) -> tuple[pd.DataFrame, dict]:
    source = config["source"]
    official = source in {"nse_bhavcopy", "bse_bhavcopy", "india_bhavcopy"}
    if official:
        from .india.loader import load as load_india
        frame, metadata = load_india(config, base)
    symbols = config["symbols"]
    if not isinstance(symbols, list) or not 1 <= len(symbols) <= 512 or len(set(symbols)) != len(symbols) or not all(isinstance(s, str) and s for s in symbols):
        raise ValueError("symbols must be 1..512 unique strings")
    start, end = pd.Timestamp(config["start"]), pd.Timestamp(config["end"])
    if start.tz is not None or end.tz is not None or start >= end:
        raise ValueError("start/end must be unzoned dates with start < end (end exclusive)")
    if config.get("interval", "1d") not in INTERVALS:
        raise ValueError("unsupported interval")
    if official:
        pass
    elif source == "csv":
        path = (base / config["path"]).resolve()
        frame = pd.read_csv(path)
        metadata = {"source": "csv", "path": str(path), "sha256": digest(path), "label": config.get("label", "User-supplied historical data")}
    elif source == "yfinance":
        import yfinance as yf
        frames = []
        for symbol in symbols:
            if not symbol.endswith((".NS", ".BO")):
                raise ValueError("Yahoo Indian cash-equity symbols must end in .NS (NSE) or .BO (BSE)")
            data = yf.Ticker(symbol).history(start=str(start.date()), end=str(end.date()),
                interval=config.get("interval", "1d"), auto_adjust=False, actions=True, raise_errors=True)
            if data.empty:
                raise ValueError(f"no data for {symbol}; vendor access or history range may be unavailable")
            # Raw bars around actions cannot be replayed safely without explicit
            # share/cash transformations. Fail closed rather than hide a jump.
            if any((data[c].fillna(0) != 0).any() for c in ("Dividends", "Stock Splits", "Capital Gains") if c in data):
                raise ValueError(f"{symbol}: corporate actions present; use an action-free period or documented adjusted CSV")
            data = data.rename(columns=str.lower).reset_index()
            data = data.rename(columns={data.columns[0]: "timestamp"})
            data["symbol"] = symbol
            frames.append(data[["timestamp", "symbol", "open", "high", "low", "close", "volume"]])
        frame = pd.concat(frames, ignore_index=True)
        metadata = {"source": "Yahoo Finance via yfinance", "version": yf.__version__, "adjustment": "raw; corporate-action periods rejected", "label": "Historical Indian cash equities"}
    else:
        raise ValueError("source must be csv, yfinance, nse_bhavcopy, bse_bhavcopy or india_bhavcopy")
    if not {"timestamp", "symbol"}.issubset(frame.columns) or frame.empty:
        raise ValueError("data needs timestamp and symbol columns and nonempty rows")
    if frame[["timestamp", "symbol"]].isna().any().any():
        raise ValueError("missing timestamp or symbol")
    # Aware timestamps are converted; naive input is explicitly IST.
    times = []
    for value in frame.timestamp:
        ts = pd.Timestamp(value)
        if pd.isna(ts):
            raise ValueError("missing timestamp")
        times.append(ts.tz_localize(TZ) if ts.tz is None else ts.tz_convert(TZ))
    frame["timestamp"] = pd.DatetimeIndex(times)
    frame = frame[frame.symbol.isin(symbols) & (frame.timestamp >= start.tz_localize(TZ)) & (frame.timestamp < end.tz_localize(TZ))].copy()
    if frame.empty or set(frame.symbol) != set(symbols):
        raise ValueError("requested symbols/date range contain no data for at least one symbol")
    # Duplicate daily dates are duplicates even if the input time differs.
    daily = config.get("mode", "bars") == "bars" and config.get("interval", "1d") == "1d"
    if daily:
        frame["timestamp"] = frame.timestamp.dt.normalize()
    if frame.duplicated(["symbol", "timestamp"]).any():
        raise ValueError("duplicate symbol/timestamp")
    holidays = set(config.get("holidays", []))
    sessions = set(config.get("extra_sessions", []))
    date = frame.timestamp.dt.strftime("%Y-%m-%d")
    bad_day = ((frame.timestamp.dt.dayofweek >= 5) & ~date.isin(sessions)) | date.isin(holidays)
    if bad_day.any():
        raise ValueError("data includes a weekend or configured holiday; configure extra_sessions for special sessions")
    if not daily:
        minutes = frame.timestamp.dt.hour * 60 + frame.timestamp.dt.minute
        seconds = frame.timestamp.dt.second + frame.timestamp.dt.microsecond / 1e6 + frame.timestamp.dt.nanosecond / 1e9
        if ((minutes < 555) | (minutes > 930) | ((minutes == 930) & (seconds > 0))).any():
            raise ValueError("data outside 09:15–15:30 IST")
    frame = frame.sort_values(["timestamp", "symbol"], kind="stable").reset_index(drop=True)
    metadata["symbols"] = symbols
    metadata["rows"] = len(frame)
    metadata["calendar"] = {"timezone": TZ, "hours": "09:15–15:30", "holidays": sorted(holidays), "extra_sessions": sorted(sessions)}
    return frame, metadata


def numeric(frame: pd.DataFrame, columns: list[str], prices: bool) -> None:
    for col in columns:
        if col not in frame:
            raise ValueError(f"missing column {col}")
        frame[col] = pd.to_numeric(frame[col], errors="raise")
        values = frame[col].to_numpy(dtype=float)
        if not np.isfinite(values).all() or (values < (0.01 if prices else 0)).any() or (values > (10_000_000 if prices else (1_000_000_000_000 if col == "volume" else 1_000_000_000))).any():
            raise ValueError(f"invalid {col}: finite positive prices / nonnegative bounded sizes required")
        if not prices and not np.equal(values, np.floor(values)).all():
            raise ValueError(f"{col} must contain integer shares")


def normalize(frame: pd.DataFrame, config: dict, path: Path) -> pd.DataFrame:
    frame = frame.copy()
    mode = config.get("mode", "bars")
    symbols = {s: i for i, s in enumerate(config["symbols"])}
    rows = []
    def add(ts, symbol, kind, ohlcv=(0, 0, 0, 0, 0), levels=None):
        row = [pd.Timestamp(ts).value, symbols[symbol], kind, *ohlcv]
        row.extend(levels if levels is not None else [0] * 20)
        rows.append(row)
    def paise(value):
        return int(np.floor(float(value) * 100 + 0.5))
    if mode == "bars":
        numeric(frame, ["open", "high", "low", "close"], True)
        numeric(frame, ["volume"], False)
        if ((frame.low > frame[["open", "close"]].min(axis=1)) | (frame.high < frame[["open", "close"]].max(axis=1))).any():
            raise ValueError("OHLC envelope invalid")
        participation = config.get("participation", 0.01)
        spread = config.get("spread_bps", 2.0)
        if not np.isfinite(participation) or not 0 < participation <= 1 or not np.isfinite(spread) or not 0 <= spread <= 1000:
            raise ValueError("invalid participation or spread")
        interval = config.get("interval", "1d")
        previous_volume, previous_end = {}, {}
        for r in frame.itertuples():
            begin = r.timestamp.normalize() + pd.Timedelta(hours=9, minutes=15) if interval == "1d" else r.timestamp
            finish = min(begin + pd.Timedelta(minutes=INTERVALS[interval]), begin.normalize() + pd.Timedelta(hours=15, minutes=30))
            if finish <= begin or (r.symbol in previous_end and begin < previous_end[r.symbol]):
                raise ValueError("overlapping or zero-length bars")
            previous_end[r.symbol] = finish
            px = paise(r.open)
            half = int(np.ceil(px * spread / 20000))
            if px - half <= 0:
                raise ValueError("spread leaves nonpositive bid")
            # Capacity is known before this bar: never use its future volume.
            size = min(1_000_000_000, int(previous_volume.get(r.symbol, 0) * participation))
            levels = [px-half, size, px+half, size] + [0]*16
            add(begin, r.symbol, 0, levels=levels)
            # A bar becomes available immediately before the next boundary.
            add(finish - pd.Timedelta(nanoseconds=1), r.symbol, 1,
                tuple(paise(x) for x in (r.open, r.high, r.low, r.close)) + (int(r.volume),))
            previous_volume[r.symbol] = int(r.volume)
    elif mode == "quotes":
        numeric(frame, ["bid", "ask"], True)
        numeric(frame, ["bid_qty", "ask_qty"], False)
        if (frame.bid > frame.ask).any():
            raise ValueError("crossed quote")
        # Optional depth levels 1..4, level zero uses unsuffixed names.
        for i in range(1, 5):
            cols = [f"bid{i}", f"bid_qty{i}", f"ask{i}", f"ask_qty{i}"]
            if any(c in frame for c in cols):
                numeric(frame, [cols[0], cols[2]], True)
                numeric(frame, [cols[1], cols[3]], False)
                prev = "" if i == 1 else str(i-1)
                if f"bid{prev}" not in frame or (frame[cols[0]] >= frame[f"bid{prev}"]).any() or (frame[cols[2]] <= frame[f"ask{prev}"]).any():
                    raise ValueError("depth must be contiguous and strictly price sorted")
        for r in frame.to_dict("records"):
            levels = []
            for i in range(5):
                suffix = "" if i == 0 else str(i)
                levels.extend([paise(r[f"bid{suffix}"]), int(r[f"bid_qty{suffix}"]), paise(r[f"ask{suffix}"]), int(r[f"ask_qty{suffix}"])] if f"bid{suffix}" in r else [0]*4)
            add(r["timestamp"], r["symbol"], 2, levels=levels)
    else:
        raise ValueError("mode must be bars or quotes; trade-only ticks do not supply executable liquidity")
    events = pd.DataFrame(rows, columns=COLUMNS).sort_values(["timestamp", "symbol", "kind"], kind="stable")
    events.to_csv(path, index=False)
    return frame
