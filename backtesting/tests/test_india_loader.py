import json
import unittest
from unittest.mock import patch
import test_india as fixtures
from test_india import UDIFF, FakeResponse
from fort_backtest.india import IndiaData
from fort_backtest.india.calendar import Calendar


class LoaderTests(unittest.TestCase):
    setUp = fixtures.CacheTests.setUp
    tearDown = fixtures.CacheTests.tearDown
    sleep = fixtures.CacheTests.sleep
    client = fixtures.CacheTests.client

    # Reuse only the cache helpers, not another network integration fixture.
    def seed(self, extra=False, last_close=104):
        master = b"SYMBOL,NAME OF COMPANY,SERIES,DATE OF LISTING,ISIN NUMBER\nRELIANCE,Reliance,EQ,01-Jan-1995,INE002A01018\n"
        holidays = json.dumps(
            {"CM": [{"tradingDate": f"{d:02}-Jan-2025"} for d in range(1, 6)]}
        ).encode()
        first = UDIFF.replace(b"2025-03-28", b"2025-03-27")
        last = UDIFF.replace(b",104,1000", f",{last_close},1000".encode()).replace(
            b",105,99,", f",{max(105, last_close)},99,".encode()
        )
        responses = [
            FakeResponse(master),
            FakeResponse(holidays),
            FakeResponse(first),
            FakeResponse(last),
        ]
        if extra:
            responses += [
                FakeResponse(
                    b.replace(b",NSE,", b",BSE,")
                    .replace(b",2885,", b",500325,")
                    .replace(b",EQ,", b",A,")
                )
                for b in [first, last]
            ]
        client = self.client(responses)
        store = IndiaData(client=client)
        store.get_symbol_master()
        cal = Calendar.load(client, "2025-03-27", "2025-03-29")
        store.download_bhavcopy("2025-03-27", "2025-03-29", calendar=cal)
        if extra:
            store.download_bhavcopy("2025-03-27", "2025-03-29", "BSE", calendar=cal)
        return {
            "source": "nse_bhavcopy",
            "symbols": ["RELIANCE"],
            "start": "2025-03-27",
            "end": "2025-03-29",
            "cache_dir": str(self.root),
            "offline": True,
        }

    def test_official_loader_offline_existing_schema(self):
        from fort_backtest.data import load, normalize

        config = self.seed()
        with patch(
            "requests.Session.get",
            side_effect=AssertionError("offline attempted network"),
        ):
            f, metadata = load(config, self.root)
            normalize(f, config, self.root / "events.csv")
        self.assertEqual(config["symbols"], ["RELIANCE.NS"])
        self.assertEqual(len(f), 2)
        self.assertEqual(metadata["quality_issues"], [])

    def test_preperiod_filters_do_not_use_future_volume(self):
        from fort_backtest.india.loader import load

        config = self.seed()
        config.update(
            start="2025-03-28",
            history_start="2025-03-27",
            min_history_sessions=1,
            min_avg_volume=1001,
        )
        with self.assertRaisesRegex(ValueError, "no symbols pass"):
            load(config, self.root)
        config["min_avg_volume"] = 1000
        f, metadata = load(config, self.root)
        self.assertEqual(metadata["profiles"][0]["history_sessions"], 1)
        self.assertEqual(len(f), 1)

    def test_price_jump_fails_or_is_reported(self):
        from fort_backtest.india.loader import load

        config = self.seed(last_close=200)
        with self.assertRaisesRegex(ValueError, "quality checks"):
            load(config, self.root)
        config["quality_policy"] = "warn"
        _, m = load(config, self.root)
        self.assertEqual(
            m["quality_issues"][0]["kind"], "possible_corporate_action_or_bad_price"
        )

    def test_dual_listing_bse_preference(self):
        from fort_backtest.india.loader import load

        config = self.seed(extra=True)
        config.update(
            source="india_bhavcopy",
            exchange="BOTH",
            calendar_exchange="NSE",
            exchange_preference="BSE",
        )
        f, m = load(config, self.root)
        self.assertEqual(config["symbols"], ["500325.BO"])
        self.assertEqual(len(f), 2)

    def test_market_cap_is_not_invented(self):
        from fort_backtest.india.loader import load

        config = self.seed()
        config["min_market_cap"] = 1000
        with self.assertRaisesRegex(ValueError, "shares outstanding"):
            load(config, self.root)

    def test_market_cap_requires_valid_prior_snapshot(self):
        from fort_backtest.india.loader import load

        config = self.seed()
        config.update(min_market_cap=100, master_file="caps.csv")
        header = "symbol,exchange,isin,name,series,is_active,listing_date,yahoo_ticker,market_cap_inr,market_cap_asof\n"
        row = "RELIANCE,NSE,INE002A01018,Reliance,EQ,true,1995-01-01,RELIANCE.NS,1000,"
        path = self.root / "caps.csv"
        path.write_text(header + row + "2025-03-26\n")
        self.assertEqual(len(load(config.copy(), self.root)[0]), 2)
        for date in ["2025-03-28", "NaT", "not-a-date"]:
            path.write_text(header + row + date + "\n")
            with self.assertRaisesRegex(ValueError, "strictly before"):
                load(config.copy(), self.root)

    def test_adjusted_not_silently_ignored(self):
        from fort_backtest.india.loader import load

        config = self.seed()
        config["adjusted"] = True
        with self.assertRaisesRegex(ValueError, "unadjusted"):
            load(config, self.root)
