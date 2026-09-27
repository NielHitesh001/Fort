"""Cached official yearly holiday snapshots plus explicit special-session overrides."""

from dataclasses import dataclass, field
import json
import pandas as pd
from .transport import InvalidData


@dataclass
class Calendar:
    holidays: set = field(default_factory=set)
    extra_sessions: set = field(default_factory=set)
    sources: list = field(default_factory=list)

    def sessions(self, start, end):
        a, b = pd.Timestamp(start), pd.Timestamp(end)
        if (
            a.tz is not None
            or b.tz is not None
            or a != a.normalize()
            or b != b.normalize()
            or a >= b
        ):
            raise ValueError("expected unzoned start/end dates; end exclusive")
        return [
            d.date()
            for d in pd.date_range(a, b, inclusive="left")
            if (
                d.date().isoformat() in self.extra_sessions
                or (d.dayofweek < 5 and d.date().isoformat() not in self.holidays)
            )
        ]

    @classmethod
    def load(
        cls,
        client,
        start,
        end,
        *,
        exchange="NSE",
        path=None,
        holidays=(),
        extra_sessions=(),
        refresh=False,
    ):
        result = cls(set(map(str, holidays)), set(map(str, extra_sessions)))
        if path:
            payload = json.loads(path.read_text())
            if payload.get("exchange") != exchange:
                raise ValueError("calendar exchange mismatch")
            years = set(
                range(
                    pd.Timestamp(start).year,
                    (pd.Timestamp(end) - pd.Timedelta(days=1)).year + 1,
                )
            )
            if not years.issubset(set(payload.get("years", []))):
                raise ValueError("calendar does not cover requested years")
            result.holidays.update(payload["holidays"])
            result.extra_sessions.update(payload.get("extra_sessions", []))
            result.sources.append({"path": str(path), "exchange": exchange})
            return result
        if exchange != "NSE":
            raise ValueError(
                "BSE requires calendar_file with verified BSE holidays; or explicitly set calendar_exchange: NSE to use the NSE cash calendar proxy"
            )
        for year in range(
            pd.Timestamp(start).year,
            (pd.Timestamp(end) - pd.Timedelta(days=1)).year + 1,
        ):
            url = (
                f"https://www.nseindia.com/api/holiday-master?type=trading&year={year}"
            )

            def parse(body):
                try:
                    records = json.loads(body)["CM"]
                    dates = pd.to_datetime(
                        [r["tradingDate"] for r in records], format="%d-%b-%Y"
                    )
                    if len(dates) < 5 or not (dates.year == year).all():
                        raise ValueError("wrong year or incomplete response")
                    return set(dates.strftime("%Y-%m-%d"))
                except (KeyError, ValueError, TypeError) as exc:
                    raise InvalidData(
                        f"invalid holiday calendar {year}: {exc}"
                    ) from exc

            body = client.get(url, f"calendars/nse-{year}.json", parse, refresh=refresh)
            result.holidays.update(parse(body))
            result.sources.append({"url": url, "year": year})
        result.holidays -= result.extra_sessions
        return result
