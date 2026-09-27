from __future__ import annotations
import argparse
from concurrent.futures import ProcessPoolExecutor
from copy import deepcopy
import importlib.metadata
import json
from pathlib import Path
import platform
import math
import subprocess
import sys
import tempfile
import numpy as np
import yaml
from . import analytics, data, report

ROOT = Path(__file__).resolve().parents[2]


def validate_parameters(cfg):
    st, ex, an = cfg["strategy"], cfg["execution"], cfg["analytics"]
    integers = [(st["lookback"], 2, 256), (st["quantity"], 1, 1_000_000_000),
        (ex["max_position"], 1, 1_000_000_000), (ex["latency_ns"], 0, 2**63-1),
        (ex["fill_capacity"], 1, 10_000_000), (an["periods_per_year"], 2, 366), (an.get("max_trade_charts",12), 0, 512)]
    if any(type(v) is not int or not lo <= v <= hi for v,lo,hi in integers):
        raise ValueError("integer strategy, capacity, latency or annualization parameter out of range")
    for value, low, high in [(ex["initial_cash"], 0, 1e15), (st["threshold"], 0, 1),
                             (an["risk_free_rate"], -1, 100)]:
        if type(value) not in (int, float) or not math.isfinite(value) or not low < value <= high:
            raise ValueError("invalid capital, threshold or risk-free rate")
    if st["threshold"] >= 1 or st["name"] not in {"mean_reversion", "momentum"}:
        raise ValueError("invalid strategy name or threshold")
    for key in ["commission_bps", "slippage_bps"]:
        value = ex[key]
        if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1000:
            raise ValueError(f"invalid {key}")


def configuration(path: Path):
    with path.open() as f:
        cfg = json.load(f) if path.suffix == ".json" else yaml.safe_load(f)
    if not isinstance(cfg, dict):
        raise ValueError("configuration must be a mapping")
    unknown = set(cfg) - {"data", "strategy", "execution", "analytics", "seed", "output", "runs", "workers"}
    if unknown:
        raise ValueError(f"unknown config keys: {unknown}")
    required = {"data": {"source", "start", "end"}, "strategy": {"name"}}
    allowed = {
        "data": {"source", "symbols", "start", "end", "path", "label", "mode", "interval", "participation", "spread_bps", "holidays", "extra_sessions", "universe", "exchange", "cache_dir", "offline", "refresh", "adjusted", "calendar_file", "calendar_exchange", "master_file", "universe_file", "include_sme", "series", "exchange_preference", "history_start", "min_history_sessions", "min_coverage", "min_avg_volume", "min_avg_turnover", "min_market_cap", "top_n", "quality_policy", "price_jump_threshold", "master_max_age_days"},
        "strategy": {"name", "lookback", "threshold", "quantity"},
        "execution": {"initial_cash", "commission_bps", "slippage_bps", "max_position", "latency_ns", "fill_capacity"},
        "analytics": {"risk_free_rate", "periods_per_year", "max_trade_charts"},
    }
    for section, keys in allowed.items():
        value = cfg.setdefault(section, {})
        if not isinstance(value, dict) or set(value) - keys or required.get(section, set()) - set(value):
            raise ValueError(f"missing or unknown keys in {section}")
    if not cfg["data"].get("symbols") and not cfg["data"].get("universe"):
        raise ValueError("data requires symbols or universe")
    if cfg["strategy"]["name"] not in {"mean_reversion", "momentum"}:
        raise ValueError("unknown strategy")
    defaults = {"strategy": {"lookback": 20, "threshold": .02, "quantity": 10},
        "execution": {"initial_cash": 1_000_000, "commission_bps": 3, "slippage_bps": 2, "max_position": 100_000, "latency_ns": 0, "fill_capacity": 100_000},
        "analytics": {"risk_free_rate": 0., "periods_per_year": 252, "max_trade_charts": 12}}
    for section, values in defaults.items():
        cfg[section] = {**values, **cfg[section]}
    validate_parameters(cfg)
    cfg.setdefault("seed", 42)
    cfg.setdefault("workers", 1)
    if type(cfg["seed"]) is not int or not 0 <= cfg["seed"] <= 2**32-1 or type(cfg["workers"]) is not int or not 1 <= cfg["workers"] <= 64:
        raise ValueError("invalid seed or workers (1..64)")
    for key in ["start", "end"]:
        cfg["data"][key] = str(cfg["data"][key])
    return cfg


def run_one(job):
    cfg, events, source, metadata, output, binary = job
    np.random.seed(cfg["seed"])
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    ex, st = cfg["execution"], cfg["strategy"]
    command = [str(binary), str(events), str(output), *map(str, [ex["initial_cash"], ex["commission_bps"], ex["slippage_bps"], ex["max_position"], ex["latency_ns"], st["lookback"], st["threshold"], st["quantity"], st["name"], ex["fill_capacity"]])]
    try:
        completed = subprocess.run(command, text=True, capture_output=True, check=True)
        versions = {p: importlib.metadata.version(p) for p in ["numpy", "pandas", "matplotlib", "PyYAML", "yfinance", "requests"]}
        manifest = {"schema_version": 1, "config": cfg, "data": metadata, "events_sha256": data.digest(Path(events)),
            "executable_sha256": data.digest(Path(binary)), "python": platform.python_version(), "dependencies": versions,
            "execution_model": "later-open bars / independent quote snapshots; price-time priority; long-only",
            "engine": json.loads(completed.stdout), "seed": cfg["seed"]}
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        (output / "config.json").write_text(json.dumps(cfg, indent=2) + "\n")
        result = analytics.export(output, ex["initial_cash"], cfg["analytics"]["risk_free_rate"], cfg["analytics"]["periods_per_year"])
        report.generate(output, source, cfg, manifest, result)
        (output / "SUCCESS").write_text("Replay and reports completed.\n")
        return {"output": str(output), **result[0]}
    except Exception as exc:
        details = exc.stderr if isinstance(exc, subprocess.CalledProcessError) else str(exc)
        (output / "FAILED.txt").write_text(details)
        raise RuntimeError(f"run failed in {output}: {details}") from exc


def main():
    parser = argparse.ArgumentParser(description="Fort Indian cash-equity historical backtester")
    parser.add_argument("config", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "fort_replay")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--universe", help="NIFTY50, NIFTY500, SENSEX, BSE100, BSE500, or ALL")
    parser.add_argument("--source", choices=["csv", "yfinance", "nse_bhavcopy", "bse_bhavcopy", "india_bhavcopy"])
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    path = args.config.resolve()
    cfg = configuration(path)
    if args.source: cfg["data"]["source"] = args.source
    if args.universe:
        cfg["data"].pop("symbols", None)
        cfg["data"]["universe"] = args.universe
    if args.offline: cfg["data"]["offline"] = True
    binary = args.binary.resolve()
    if not binary.is_file():
        raise ValueError("build fort_replay first: cmake --build build --target fort_replay")
    parent = (args.output or (path.parent / cfg.get("output", "../runs"))).resolve()
    parent.mkdir(parents=True, exist_ok=True)
    # Reserve a unique directory before workers start; identical configurations
    # never overwrite one another. Download once, replay a frozen shared file.
    batch = Path(tempfile.mkdtemp(prefix="backtest-", dir=parent))
    frame, metadata = data.load(cfg["data"], path.parent)
    events = batch / "events.csv"
    source = data.normalize(frame, cfg["data"], events)
    source.to_csv(batch / "source.csv", index=False)
    metadata["source_snapshot_sha256"] = data.digest(batch / "source.csv")
    metadata["source_snapshot"] = str(batch / "source.csv")
    variants = cfg.get("runs", [{}])
    if not isinstance(variants, list) or not variants or any(not isinstance(v, dict) or set(v)-{"lookback", "threshold", "quantity", "name"} for v in variants):
        raise ValueError("runs must be a nonempty list of strategy parameter overrides")
    jobs = []
    for i, variant in enumerate(variants):
        local = deepcopy(cfg); local.pop("runs", None); local["strategy"].update(variant)
        validate_parameters(local)
        jobs.append((local, events, source, metadata, batch / f"run-{i:03d}", binary))
    if cfg["workers"] > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=min(cfg["workers"], len(jobs))) as pool:
            results = list(pool.map(run_one, jobs))
    else:
        results = [run_one(job) for job in jobs]
    (batch / "comparison.json").write_text(json.dumps(results, indent=2, allow_nan=False) + "\n")
    print(json.dumps(results, indent=2, allow_nan=False))

