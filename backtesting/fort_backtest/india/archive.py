"""Daily archive collection and resumable normalized per-day storage."""

from dataclasses import dataclass, field
from datetime import date
import json
import pandas as pd
from .calendar import Calendar
from .parsers import parse_bhavcopy, parse_indices
from .transport import Client, DownloadError, InvalidData, atomic_write, sha

SWITCH = date(2024, 7, 8)
MONTHS = (
    "JAN",
    "FEB",
    "MAR",
    "APR",
    "MAY",
    "JUN",
    "JUL",
    "AUG",
    "SEP",
    "OCT",
    "NOV",
    "DEC",
)


def archive_url(day, exchange="NSE", kind="equity"):
    exchange = exchange.upper()
    day = pd.Timestamp(day).date()
    if kind == "indices":
        if exchange != "NSE":
            raise ValueError("index close archive currently supports NSE only")
        return f"https://nsearchives.nseindia.com/content/indices/ind_close_all_{day:%d%m%Y}.csv"
    if kind != "equity" or exchange not in ("NSE", "BSE"):
        raise ValueError("invalid archive type/exchange")
    if day >= SWITCH:
        if exchange == "NSE":
            return f"https://nsearchives.nseindia.com/content/cm/BhavCopy_NSE_CM_0_0_0_{day:%Y%m%d}_F_0000.csv.zip"
        return f"https://www.bseindia.com/download/BhavCopy/Equity/BhavCopy_BSE_CM_0_0_0_{day:%Y%m%d}_F_0000.CSV"
    if exchange == "NSE":
        mon = MONTHS[day.month - 1]
        return f"https://nsearchives.nseindia.com/content/historical/EQUITIES/{day.year}/{mon}/cm{day:%d}{mon}{day.year}bhav.csv.zip"
    return f"https://www.bseindia.com/download/BhavCopy/Equity/EQ{day:%d%m%y}_CSV.ZIP"


@dataclass
class DownloadResult:
    files: list = field(default_factory=list)
    failures: list = field(default_factory=list)
    sessions: list = field(default_factory=list)
    provenance: list = field(default_factory=list)


class IndiaData:
    def __init__(self, root="backtesting/cache/india", *, offline=False, client=None):
        self.client = client or Client(root, offline=offline)
        self.root = self.client.root

    def get_symbol_master(self, exchange="NSE", **kwargs):
        from .masters import get_symbol_master

        return get_symbol_master(self.client, exchange, **kwargs)

    def download_bhavcopy(
        self,
        start_date,
        end_date,
        exchange="NSE",
        *,
        calendar=None,
        refresh=False,
        kind="equity",
    ):
        exchange = exchange.upper()
        if calendar is None:
            calendar = Calendar.load(
                self.client, start_date, end_date, exchange=exchange
            )
        sessions = calendar.sessions(start_date, end_date)
        result = DownloadResult(sessions=[str(d) for d in sessions])
        parser = (
            (lambda b, d: parse_indices(b, d))
            if kind == "indices"
            else (lambda b, d: parse_bhavcopy(b, exchange, d))
        )
        for day in sessions:
            url = archive_url(day, exchange, kind)
            rel = f"raw/{exchange.lower()}/{kind}/{day:%Y/%m}/{url.rsplit('/', 1)[1]}"
            try:
                body = self.client.get(
                    url, rel, lambda b: parser(b, day), refresh=refresh
                )
                raw_hash = sha(body)
                target = (
                    self.root
                    / f"processed/{exchange.lower()}/{kind}/{day}-{raw_hash}.csv"
                )
                meta_path = target.with_suffix(".json")
                try:
                    meta = json.loads(meta_path.read_text())
                except (OSError, ValueError):
                    meta = {}
                if not (
                    target.exists()
                    and meta.get("raw_sha256") == raw_hash
                    and meta.get("parser_version") == 2
                    and meta.get("sha256") == sha(target.read_bytes())
                ):
                    frame = parser(body, day)
                    content = frame.to_csv(index=False).encode()
                    atomic_write(target, content)
                    meta = {
                        "url": url,
                        "raw_sha256": raw_hash,
                        "sha256": sha(content),
                        "parser_version": 2,
                    }
                    atomic_write(meta_path, json.dumps(meta).encode())
                result.files.append(target)
                result.provenance.append(
                    {"date": str(day), "exchange": exchange, **meta}
                )
            except (DownloadError, InvalidData, ValueError) as exc:
                failure = {
                    "date": str(day),
                    "exchange": exchange,
                    "url": url,
                    "error": str(exc),
                }
                result.failures.append(failure)
                self.client._record(status="day_failed", **failure)
        content = json.dumps(
            {
                "exchange": exchange,
                "start": str(start_date),
                "end": str(end_date),
                "kind": kind,
                "sessions": result.sessions,
                "failures": result.failures,
                "provenance": result.provenance,
            },
            indent=2,
        ).encode()
        atomic_write(
            self.root
            / f"manifests/{exchange.lower()}-{kind}-{start_date}-{end_date}.json",
            content,
        )
        return result

    @staticmethod
    def read_files(paths, symbols=None):
        if not paths:
            raise ValueError("no cached trading-day files available")
        frames = []
        for path in paths:
            frame = pd.read_csv(
                path,
                dtype={"symbol": str, "isin": str, "series": str, "name": str},
                keep_default_na=False,
            )
            if symbols is not None:
                frame = frame[frame.symbol.isin(symbols)]
            frames.append(frame)
        return pd.concat(frames, ignore_index=True)

    def load_ohlcv(
        self,
        symbol,
        start,
        end,
        adjusted=False,
        *,
        exchange="NSE",
        calendar=None,
        series=None,
    ):
        if adjusted:
            raise ValueError(
                "official bhavcopy is unadjusted; no adjustment ledger is installed"
            )
        result = self.download_bhavcopy(start, end, exchange, calendar=calendar)
        if result.failures:
            raise DownloadError(
                f"incomplete history ({len(result.failures)} days); see cache manifests"
            )
        symbol = symbol.removesuffix(".NS").removesuffix(".BO")
        frame = self.read_files(result.files, symbols=[symbol])
        out = frame[frame.symbol.eq(symbol)].copy()
        if series:
            out = out[out.series.eq(series)]
        elif exchange.upper() == "NSE":
            out = out[out.series.eq("EQ")]
        if out.empty or out.date.duplicated().any():
            raise ValueError("missing/ambiguous symbol series")
        if out.quality_issue.ne("").any():
            raise InvalidData("selected symbol contains invalid OHLC rows")
        out["date"] = pd.to_datetime(out.date)
        out = out.set_index("date").sort_index()
        out.attrs.update(
            {
                "adjusted": False,
                "currency": "INR",
                "timezone": "Asia/Kolkata",
                "missing_sessions": sorted(
                    set(result.sessions) - set(out.index.strftime("%Y-%m-%d"))
                ),
                "possible_corporate_action_sessions": out.index[
                    pd.to_numeric(out.close).pct_change().abs().gt(0.35)
                ]
                .strftime("%Y-%m-%d")
                .tolist(),
                "zero_volume_sessions": out.index[out.volume.eq(0)]
                .strftime("%Y-%m-%d")
                .tolist(),
            }
        )
        return out
