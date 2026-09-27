"""Post-replay accounting statistics. No work here runs on the C++ hot path."""
from __future__ import annotations
import json
from pathlib import Path
import numpy as np
import pandas as pd
from .data import TZ


def round_trips(fills: pd.DataFrame) -> pd.DataFrame:
    records, active = [], {}
    for f in fills.itertuples():
        state = active.setdefault(f.symbol, {"symbol": f.symbol, "entry": f.timestamp, "quantity": 0, "pnl": 0., "commission": 0., "slippage": 0.})
        state["quantity"] += f.qty if f.side == "buy" else -f.qty
        state["pnl"] += f.realized
        state["commission"] += f.commission
        state["slippage"] += f.slippage
        if state["quantity"] == 0:
            records.append({**state, "exit": f.timestamp})
            del active[f.symbol]
    return pd.DataFrame(records, columns=["symbol", "entry", "exit", "quantity", "pnl", "commission", "slippage"])


def calculate(equity: pd.DataFrame, fills: pd.DataFrame, initial: float, annual_risk_free: float = 0., periods: int = 252):
    if not np.isfinite(annual_risk_free) or annual_risk_free <= -1 or periods <= 1:
        raise ValueError("invalid annual risk-free rate / periods")
    e = equity.copy()
    e["date"] = pd.to_datetime(e.timestamp, unit="ns", utc=True).dt.tz_convert(TZ)
    daily = e.groupby(e.date.dt.normalize(), sort=True).last().drop(columns=["date"])
    previous = daily.equity.shift(1, fill_value=initial)
    daily["pnl"] = daily.equity - previous
    daily["return"] = daily.equity / previous - 1
    r = daily["return"].to_numpy()
    excess = r - ((1 + annual_risk_free)**(1 / periods) - 1)
    deviation = np.std(excess, ddof=1) if len(r) > 1 else 0
    downside = np.sqrt(np.mean(np.minimum(excess, 0)**2))
    values = np.r_[initial, e.equity.to_numpy()]
    times = pd.DatetimeIndex([e.date.iloc[0], *e.date])
    peaks = np.maximum.accumulate(values)
    drawdown = values / peaks - 1
    peak_time = times[0]
    duration = pd.Timedelta(0)
    underwater = False
    for ts, dd in zip(times, drawdown):
        if underwater:
            duration = max(duration, ts - peak_time)
        if dd >= -1e-12:
            peak_time = ts
        else:
            duration = max(duration, ts - peak_time)
        underwater = dd < -1e-12
    total_return = values[-1] / initial - 1
    days = (daily.index[-1] - daily.index[0]).days + 1
    # Calendar-time CAGR; undefined for <=1 observed day or nonpositive capital.
    growth = np.log(values[-1] / initial) * 365.25 / days if values[-1] > 0 else np.nan
    annual = float(np.expm1(growth)) if len(daily) > 1 and np.isfinite(growth) and growth < 700 else None
    trips = round_trips(fills)
    winners = trips.loc[trips.pnl > 0, "pnl"]
    losers = trips.loc[trips.pnl < 0, "pnl"]
    metrics = {
        "initial_capital_inr": initial, "final_equity_inr": values[-1],
        "net_pnl_inr": values[-1] - initial, "total_return": total_return,
        "annualized_return": annual,
        "sharpe_ratio": float(np.mean(excess) / deviation * np.sqrt(periods)) if deviation > 1e-15 else None,
        "sortino_ratio": float(np.mean(excess) / downside * np.sqrt(periods)) if downside > 1e-15 else None,
        "max_drawdown": float(-drawdown.min()), "max_drawdown_duration_days": duration.total_seconds() / 86400,
        "completed_round_trips": len(trips), "win_rate": len(winners) / len(trips) if len(trips) else None,
        "average_win_inr": float(winners.mean()) if len(winners) else None,
        "average_loss_inr": float(losers.mean()) if len(losers) else None,
        "commission_inr": float(fills.commission.sum()), "slippage_inr": float(fills.slippage.sum()),
        "realized_pnl_inr": float(fills.realized.sum()), "observed_sessions": len(daily), "fill_count": len(fills),
    }
    e["drawdown"] = drawdown[1:]
    return metrics, daily, e, trips


def export(directory: Path, initial: float, risk_free: float, periods: int):
    equity = pd.read_csv(directory / "equity.csv")
    fills = pd.read_csv(directory / "trades.csv")
    result = calculate(equity, fills, initial, risk_free, periods)
    metrics, daily, _, trips = result
    (directory / "metrics.json").write_text(json.dumps(metrics, indent=2, allow_nan=False) + "\n")
    daily.to_csv(directory / "daily_pnl.csv", index_label="date")
    trips.to_csv(directory / "round_trips.csv", index=False)
    return result
