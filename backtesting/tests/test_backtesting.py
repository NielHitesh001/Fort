import json
from pathlib import Path
import tempfile
import os
import subprocess
import sys
import unittest
import numpy as np
import pandas as pd
from fort_backtest.data import load, normalize, TZ
from fort_backtest.analytics import calculate
from fort_backtest.cli import configuration


class DataTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cfg = dict(source="csv", path="bars.csv", symbols=["TEST.NS"], start="2025-02-03", end="2025-02-06", interval="1d", mode="bars")
        self.frame = pd.DataFrame({"timestamp": ["2025-02-03", "2025-02-04", "2025-02-05"], "symbol": ["TEST.NS"]*3,
            "open": [100,101,102], "high": [103,104,105], "low": [99,100,101], "close": [102,103,104], "volume": [1000,2000,3000]})
    def tearDown(self):
        self.tmp.cleanup()
    def read(self, frame=None):
        (self.frame if frame is None else frame).to_csv(self.root / "bars.csv", index=False)
        return load(self.cfg, self.root)[0]
    def test_next_open_no_future_volume(self):
        frame = self.read()
        normalize(frame, self.cfg, self.root/"events.csv")
        e = pd.read_csv(self.root/"events.csv")
        self.assertEqual(e.kind.tolist(), [0,1,0,1,0,1])
        self.assertEqual(e[e.kind==0].ask_qty0.tolist(), [0,10,20])
        self.assertEqual(e[e.kind==0].close.tolist(), [0,0,0])
        time = pd.to_datetime(e.timestamp,unit="ns",utc=True).dt.tz_convert(TZ)
        self.assertEqual(time.iloc[0].strftime("%H:%M"),"09:15")
        self.assertEqual(time.iloc[1].strftime("%H:%M"),"15:29")
    def test_duplicates(self):
        with self.assertRaisesRegex(ValueError,"duplicate"):
            self.read(pd.concat([self.frame,self.frame.iloc[:1]]))
    def test_nan(self):
        frame = self.read(); frame.loc[0,"close"] = np.nan
        with self.assertRaises(ValueError): normalize(frame,self.cfg,self.root/"events.csv")
    def test_envelope_and_volume(self):
        for field,value in [("high",1),("volume",-1),("volume",1.5),("close",float("inf"))]:
            frame = self.read(); frame[field] = frame[field].astype(float); frame.loc[0,field]=value
            with self.assertRaises(ValueError): normalize(frame,self.cfg,self.root/"events.csv")
    def test_timezone_and_hours(self):
        self.cfg.update(interval="1m")
        self.frame.timestamp = ["2025-02-03T03:45:00Z","2025-02-04T03:45:00Z","2025-02-05T03:45:00Z"]
        self.assertEqual(self.read().timestamp.iloc[0].hour,9)
        self.frame.loc[0,"timestamp"]="2025-02-03T03:44:00Z"
        with self.assertRaisesRegex(ValueError,"outside"): self.read()
    def test_weekend_and_holiday(self):
        self.cfg["end"]="2025-02-10"; self.frame.loc[0,"timestamp"]="2025-02-08"
        with self.assertRaises(ValueError): self.read()
        self.cfg["extra_sessions"]=["2025-02-08"]
        self.assertEqual(len(self.read()),3)
        self.cfg["holidays"]=["2025-02-04"]
        with self.assertRaises(ValueError): self.read()
    def test_missing_symbol(self):
        self.cfg["symbols"].append("MISSING.BO")
        with self.assertRaisesRegex(ValueError,"no data"): self.read()
    def test_intraday_boundary(self):
        self.cfg["interval"]="5m"
        self.frame.timestamp=["2025-02-03 09:15","2025-02-03 09:20","2025-02-03 15:25"]
        normalize(self.read(),self.cfg,self.root/"events.csv")
        e=pd.read_csv(self.root/"events.csv")
        self.assertEqual(e.timestamp.iloc[2]-e.timestamp.iloc[1],1)
    def test_overlapping_bars(self):
        self.cfg["interval"]="5m"; self.frame.timestamp=["2025-02-03 09:15","2025-02-03 09:16","2025-02-03 09:20"]
        with self.assertRaisesRegex(ValueError,"overlapping"): normalize(self.read(),self.cfg,self.root/"events.csv")
    def test_quotes(self):
        self.cfg.update(mode="quotes",interval="1m")
        q=pd.DataFrame(dict(timestamp=["2025-02-03 09:15"],symbol=["TEST.NS"],bid=[100],ask=[101],bid_qty=[10],ask_qty=[20]))
        normalize(self.read(q),self.cfg,self.root/"events.csv")
        e=pd.read_csv(self.root/"events.csv")
        self.assertEqual(e.bid0.iloc[0],10000); self.assertEqual(e.kind.iloc[0],2)
        q.loc[0,"ask"]=99
        with self.assertRaisesRegex(ValueError,"crossed"): normalize(self.read(q),self.cfg,self.root/"events.csv")
    def test_config_typo(self):
        p=self.root/"config.json"; p.write_text(json.dumps({"data":self.cfg,"strategy":{"name":"momentum","lookbak":4}}))
        with self.assertRaisesRegex(ValueError,"unknown"): configuration(p)


class MetricsTests(unittest.TestCase):
    def equity(self, values):
        timestamps=pd.date_range("2025-02-03 15:30",periods=len(values),freq="D",tz=TZ)
        return pd.DataFrame(dict(timestamp=[t.value for t in timestamps],cash=values,equity=values,commission=[0]*len(values),slippage=[0]*len(values)))
    def empty(self):
        return pd.DataFrame(columns=["timestamp","symbol","side","qty","realized","commission","slippage","closed_qty"])
    def test_marked_returns_and_drawdown(self):
        m,d,e,t=calculate(self.equity([100,120,90,110]),self.empty(),100)
        self.assertAlmostEqual(m["total_return"],.1)
        self.assertAlmostEqual(m["max_drawdown"],.25)
        self.assertEqual(m["max_drawdown_duration_days"],2)
        self.assertAlmostEqual(np.prod(1+d["return"]),1.1)
        self.assertIsNone(m["win_rate"])
    def test_no_trades_and_zero_variance(self):
        m,*_=calculate(self.equity([100,100,100]),self.empty(),100)
        self.assertIsNone(m["sharpe_ratio"]); self.assertIsNone(m["sortino_ratio"])
        self.assertEqual(m["max_drawdown"],0)
        self.assertEqual(m["annualized_return"],0)
    def test_recovered_drawdown_duration(self):
        m,*_=calculate(self.equity([100,90,100]),self.empty(),100)
        self.assertEqual(m["max_drawdown_duration_days"],2)
    def test_initial_capital_drawdown(self):
        m,*_=calculate(self.equity([90]),self.empty(),100)
        self.assertAlmostEqual(m["max_drawdown"],.1)
        self.assertIsNone(m["annualized_return"])
    def test_roundtrip_partial_and_open(self):
        fills=pd.DataFrame([
            [1,0,"buy",10,0,1,.5,0], [2,0,"sell",4,4,.4,.2,4], [3,0,"sell",6,-2,.6,.3,6],
            [4,1,"buy",1,0,1,0,0]],columns=self.empty().columns)
        m,_,_,t=calculate(self.equity([100,101,102,101]),fills,100)
        self.assertEqual(m["completed_round_trips"],1)
        self.assertEqual(m["win_rate"],1)
        self.assertEqual(m["average_win_inr"],2)
        self.assertEqual(m["commission_inr"],3)
    def test_daily_grouping_and_riskfree(self):
        e=self.equity([100,105,110]); e.loc[1,"timestamp"]=e.loc[0,"timestamp"]+1
        m,d,*_=calculate(e,self.empty(),100,.05)
        self.assertEqual(m["observed_sessions"],2)
        self.assertAlmostEqual(d["return"].iloc[0],.05)

class IntegrationTests(unittest.TestCase):
    def test_parallel_replay_is_reproducible(self):
        root = Path(__file__).resolve().parents[2]
        binary = root / "build" / "fort_replay"
        if not binary.exists():
            self.skipTest("build fort_replay to enable end-to-end integration")
        cfg = json.loads((root / "backtesting/examples/demo.json").read_text())
        cfg["data"]["path"] = str(root / "backtesting/examples/synthetic.csv")
        cfg.update(workers=2, runs=[{}, {}])
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "config.json"
            config.write_text(json.dumps(cfg))
            env = {**os.environ, "PYTHONPATH": str(root / "backtesting")}
            run = subprocess.run([sys.executable, "-m", "fort_backtest", str(config), "--binary", str(binary), "--output", str(Path(directory)/"output")], env=env, text=True, capture_output=True, timeout=90)
            self.assertEqual(run.returncode, 0, run.stderr)
            results = json.loads(run.stdout)
            a, b = [Path(r["output"]) for r in results]
            for name in ["equity.csv", "trades.csv", "positions.csv", "metrics.json", "daily_pnl.csv", "round_trips.csv"]:
                self.assertEqual((a/name).read_bytes(), (b/name).read_bytes(), name)
            for name in ["report.html", "report.pdf", "equity.png", "equity.svg", "underwater.png", "returns_heatmap.png", "return_distribution.png", "trades_0.png", "trades_1.png", "SUCCESS"]:
                self.assertTrue((a/name).is_file(), name)


if __name__ == "__main__": unittest.main()
