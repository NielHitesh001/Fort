"""Portable HTML and vector PDF reports, plus PNG/SVG figures."""
from __future__ import annotations
import base64
import html
import json
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.ticker import PercentFormatter, StrMethodFormatter
from .data import TZ

NOTES = ("Long-only cash equities; open positions are marked, not forcibly liquidated. "
         "Bars execute only at a later open using modeled spread and previous-bar volume capacity. "
         "Quotes are independent liquidity snapshots; no historical queue position is inferred. "
         "Sharpe/Sortino use observed daily returns and configured trading periods. "
         "CAGR uses elapsed calendar days. Drawdown uses every replay sample; duration includes unrecovered drawdowns. "
         "Win/loss statistics use completed flat-to-flat round trips, after fees. "
         "Commission is a user-supplied aggregate rate, not an Indian statutory tax schedule. "
         "Slippage is embedded in prices; its displayed cost must not be subtracted again. "
         "Missing sessions are not fabricated. Calendar exceptions must be supplied. "
         "No corporate-action ledger, borrow, leverage, auctions, or market impact model is included.")


def format_metric(key, value):
    if value is None:
        return "N/A"
    if key in {"total_return", "annualized_return", "max_drawdown", "win_rate"}:
        return f"{value:.2%}"
    if isinstance(value, int):
        return str(value)
    return f"{value:,.2f}"


def generate(directory: Path, source: pd.DataFrame, config: dict, manifest: dict, result):
    metrics, daily, equity, _ = result
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11, "axes.titlesize": 15,
        "axes.spines.top": False, "axes.spines.right": False, "figure.figsize": (11, 5.5), "savefig.dpi": 220})
    charts = []
    label = manifest["data"]["label"]
    subtitle = f"{label} | {config['data']['start']} to {config['data']['end']} (exclusive)"
    with PdfPages(directory / "report.pdf", metadata={"Title": "Fort historical backtest", "Author": "Fort", "CreationDate": None, "ModDate": None}) as pdf:
        fig = plt.figure(figsize=(8.27, 11.69))
        fig.text(.09, .94, "FORT / BACKTEST REPORT", fontsize=20, weight="bold")
        fig.text(.09, .905, subtitle, fontsize=9, wrap=True)
        fig.text(.09, .88, "Strategy: " + config["strategy"]["name"], fontsize=12)
        y = .84
        for k, value in metrics.items():
            fig.text(.09, y, k.replace("_", " ").capitalize(), fontsize=10)
            fig.text(.88, y, format_metric(k, value), fontsize=10, ha="right")
            y -= .026
        import textwrap
        fig.text(.09, y-.02, textwrap.fill(NOTES, 100), fontsize=8, va="top", linespacing=1.5)
        pdf.savefig(fig); plt.close(fig)
        def save(fig, name, title):
            fig.suptitle(title, x=.08, ha="left", weight="bold", fontsize=16)
            fig.text(.08, .025, subtitle, fontsize=8, color="#555555")
            fig.tight_layout(rect=(0, .05, 1, .94))
            fig.savefig(directory / f"{name}.png")
            fig.savefig(directory / f"{name}.svg")
            pdf.savefig(fig)
            charts.append((name, title))
            plt.close(fig)
        blue, orange = "#24649a", "#c37428"
        fig, ax = plt.subplots()
        ax.plot(equity.date, equity.equity - metrics["initial_capital_inr"], color=blue, lw=1.6)
        ax.axhline(0, color="gray", lw=.7)
        ax.set(xlabel="Date (IST)", ylabel="Cumulative net P&L (INR)")
        ax.yaxis.set_major_formatter(StrMethodFormatter("{x:,.0f}"))
        save(fig, "equity", "Marked portfolio P&L after execution costs")
        fig, ax = plt.subplots()
        ax.fill_between(equity.date, equity.drawdown, 0, color=orange, alpha=.7)
        ax.set(xlabel="Date (IST)", ylabel="Drawdown from peak")
        ax.yaxis.set_major_formatter(PercentFormatter(1))
        save(fig, "underwater", "Portfolio drawdown, including open positions")
        returns = daily["return"]
        monthly = (1 + returns).groupby([returns.index.year, returns.index.month]).prod() - 1
        annual = (1 + returns).groupby(returns.index.year).prod() - 1
        years = sorted(set(returns.index.year))
        grid = np.full((len(years), 13), np.nan)
        for (year, month), value in monthly.items(): grid[years.index(year), month-1] = value
        for year, value in annual.items(): grid[years.index(year), 12] = value
        fig, ax = plt.subplots(figsize=(12, max(3.5, len(years) * .5 + 2)))
        limit = max(float(np.nanmax(np.abs(grid))), .001)
        im = ax.imshow(np.ma.masked_invalid(grid), cmap="PuOr", vmin=-limit, vmax=limit, aspect="auto")
        for row in range(len(years)):
            for col in range(13):
                if np.isfinite(grid[row,col]): ax.text(col, row, f"{grid[row,col]:.1%}", ha="center", va="center", fontsize=8,
                    color="white" if abs(grid[row,col]) > limit*.55 else "black")
        ax.set_xticks(range(13), ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec","Year"])
        ax.set_yticks(range(len(years)), years)
        fig.colorbar(im, ax=ax, format=PercentFormatter(1), pad=.02)
        save(fig, "returns_heatmap", "Compounded monthly and yearly returns (partial periods included)")
        fig, ax = plt.subplots()
        ax.hist(returns, bins=min(40, max(5, int(np.sqrt(len(returns))))), color=blue, edgecolor="white")
        ax.axvline(0, color=orange, linestyle="--", lw=1)
        ax.set(xlabel="Daily portfolio return", ylabel="Session count")
        ax.xaxis.set_major_formatter(PercentFormatter(1))
        save(fig, "return_distribution", "Distribution of observed daily returns")
        fills = pd.read_csv(directory / "trades.csv")
        for index, symbol in enumerate(config["data"]["symbols"]):
            frame = source[source.symbol == symbol]
            fig, ax = plt.subplots()
            price = frame.close if "close" in frame else (frame.bid + frame.ask)/2
            ax.plot(frame.timestamp, price, color="#667480", lw=1, label="Close / quote midpoint")
            for side, marker, color in [("buy", "^", blue), ("sell", "v", orange)]:
                sub = fills[(fills.symbol == index) & (fills.side == side)]
                time = pd.to_datetime(sub.timestamp, unit="ns", utc=True).dt.tz_convert(TZ)
                ax.scatter(time, sub.price, marker=marker, color=color, s=35, label=f"{side.title()} fills", zorder=3)
            ax.set(xlabel="Date (IST)", ylabel="Price (INR)")
            ax.legend(loc="best", fontsize=9)
            save(fig, f"trades_{index}", f"{symbol}: actual simulated entry and exit fills")
    rows = "".join(f"<tr><th>{html.escape(k.replace('_', ' '))}</th><td>{format_metric(k,v)}</td></tr>" for k,v in metrics.items())
    figures = "".join(f'<figure><img alt="{html.escape(title)}" src="data:image/png;base64,{base64.b64encode((directory / (name + ".png")).read_bytes()).decode()}"><figcaption>{html.escape(title)}</figcaption></figure>' for name,title in charts)
    page = f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Fort backtest report</title>
<style>body{{font:16px/1.6 system-ui,sans-serif;color:#172b3e;max-width:1100px;margin:40px auto;padding:0 24px}}h1{{font-size:38px}}table{{border-collapse:collapse;width:100%}}th,td{{padding:7px;border-bottom:1px solid #ddd;text-align:left}}td{{text-align:right}}img{{width:100%}}figure{{margin:35px 0}}figcaption{{color:#526273}}pre{{white-space:pre-wrap;overflow-wrap:anywhere;background:#f2f5f8;padding:20px}}.note{{padding:20px;background:#f2f5f8}}@media print{{figure{{break-inside:avoid}}}}</style>
<h1>Fort · Historical backtest</h1><p>{html.escape(subtitle)}</p><p>Strategy: {html.escape(config['strategy']['name'])}</p><table>{rows}</table>
<p class="note">{html.escape(NOTES)}</p>{figures}<h2>Daily portfolio data</h2>{daily.to_html(float_format=lambda x: f'{x:.6f}')}<h2>Reproducibility manifest</h2><pre>{html.escape(json.dumps(manifest, indent=2))}</pre></html>'''
    (directory / "report.html").write_text(page)
