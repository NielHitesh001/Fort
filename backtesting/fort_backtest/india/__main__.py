"""Collect once, replay offline: python -m fort_backtest.india --help."""

import argparse
import json
import logging
from pathlib import Path
import sys
from .archive import IndiaData
from .calendar import Calendar
from .masters import constituents
from .transport import DownloadError


def main():
    p = argparse.ArgumentParser(
        description="Official NSE/BSE EOD archive and symbol-master cache"
    )
    p.add_argument("--cache", type=Path, default=Path("backtesting/cache/india"))
    p.add_argument("--offline", action="store_true")
    p.add_argument(
        "--refresh", action="store_true", help="explicitly refresh cached snapshots"
    )
    p.add_argument("--verbose", action="store_true")
    sub = p.add_subparsers(dest="command", required=True)
    m = sub.add_parser("master")
    m.add_argument("--exchange", choices=["NSE", "BSE"], default="NSE")
    m.add_argument("--include-sme", action="store_true")
    m.add_argument("--master-file", type=Path)
    m.add_argument(
        "--max-age-days",
        type=float,
        help="refresh automatically when older than this age",
    )
    u = sub.add_parser("universe")
    u.add_argument("name")
    u.add_argument("--universe-file", type=Path)
    for name in ["download", "load"]:
        c = sub.add_parser(name)
        c.add_argument("--start", required=True)
        c.add_argument("--end", required=True, help="exclusive")
        c.add_argument("--exchange", choices=["NSE", "BSE"], default="NSE")
        c.add_argument("--calendar-file", type=Path)
        c.add_argument(
            "--calendar-exchange",
            choices=["NSE", "BSE"],
            help="explicit calendar proxy, when needed",
        )
        c.add_argument("--extra-session", action="append", default=[])
        if name == "download":
            c.add_argument("--kind", choices=["equity", "indices"], default="equity")
        else:
            c.add_argument("symbol")
            c.add_argument("--output", type=Path, required=True)
            c.add_argument("--series")
    args = p.parse_args()
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING)
    store = IndiaData(args.cache, offline=args.offline)
    if args.command == "master":
        f = store.get_symbol_master(
            args.exchange,
            refresh=args.refresh,
            include_sme=args.include_sme,
            master_file=args.master_file,
            max_age_days=args.max_age_days,
        )
        print(
            json.dumps({"rows": len(f), "master_versions": str(store.root / "masters")})
        )
        return 0
    if args.command == "universe":
        f = constituents(
            store.client,
            args.name,
            refresh=args.refresh,
            universe_file=args.universe_file,
        )
        print(
            json.dumps(
                {"universe": args.name, "count": len(f), "symbols": f.symbol.tolist()}
            )
        )
        return 0
    cal = Calendar.load(
        store.client,
        args.start,
        args.end,
        exchange=args.calendar_exchange or args.exchange,
        path=args.calendar_file,
        extra_sessions=args.extra_session,
        refresh=args.refresh,
    )
    if args.command == "download":
        r = store.download_bhavcopy(
            args.start,
            args.end,
            args.exchange,
            calendar=cal,
            refresh=args.refresh,
            kind=args.kind,
        )
        print(
            json.dumps(
                {
                    "downloaded_or_cached": len(r.files),
                    "sessions": len(r.sessions),
                    "failures": r.failures,
                },
                indent=2,
            )
        )
        return 1 if r.failures else 0
    f = store.load_ohlcv(
        args.symbol,
        args.start,
        args.end,
        exchange=args.exchange,
        calendar=cal,
        series=args.series,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    f.rename_axis("timestamp").to_csv(args.output)
    args.output.with_suffix(args.output.suffix + ".metadata.json").write_text(
        json.dumps(f.attrs, indent=2)
    )
    print(json.dumps({"rows": len(f), "output": str(args.output)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (DownloadError, ValueError, OSError) as exc:
        print(f"India data: {exc}", file=sys.stderr)
        raise SystemExit(1)
