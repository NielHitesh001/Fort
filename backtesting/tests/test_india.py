import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import pandas as pd
from fort_backtest.india.transport import (
    Client,
    MissingFile,
    Blocked,
    OfflineMiss,
    InvalidData,
)
from fort_backtest.india.parsers import parse_bhavcopy, parse_indices, csv_bytes
from fort_backtest.india.calendar import Calendar
from fort_backtest.india.archive import archive_url, IndiaData
from fort_backtest.india.masters import master_csv, prefer_exchange, constituents

LEGACY = b"SYMBOL,SERIES,OPEN,HIGH,LOW,CLOSE,TOTTRDQTY,TOTTRDVAL,TIMESTAMP,ISIN\nRELIANCE,EQ,100,105,99,104,1000,104000,05-JUL-2024,INE002A01018\nTCS,EQ,200,205,199,204,2000,408000,05-JUL-2024,INE467B01029\nSBIN,EQ,300,305,299,304,3000,912000,05-JUL-2024,INE062A01020\n"
UDIFF = b"TradDt,BizDt,Sgmt,Src,FinInstrmTp,FinInstrmId,ISIN,TckrSymb,SctySrs,FinInstrmNm,OpnPric,HghPric,LwPric,ClsPric,TtlTradgVol,TtlTrfVal\n2025-03-28,2025-03-28,CM,NSE,STK,2885,INE002A01018,RELIANCE,EQ,Reliance,100,105,99,104,1000,104000\n"
BSE = b"SC_CODE,SC_NAME,SC_GROUP,SC_TYPE,OPEN,HIGH,LOW,CLOSE,NO_OF_SHRS,NET_TURNOV\n500325,RELIANCE,A,Q,100,105,99,104,1000,104000\n"


def zipped(body, name="file.csv"):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr(name, body)
    return out.getvalue()


class ParserTests(unittest.TestCase):
    def test_nse_legacy_major_names(self):
        f = parse_bhavcopy(zipped(LEGACY), "NSE", "2024-07-05")
        self.assertEqual(set(f.symbol), {"RELIANCE", "TCS", "SBIN"})
        self.assertEqual(f.volume.sum(), 6000)

    def test_udiff_both_exchanges(self):
        f = parse_bhavcopy(zipped(UDIFF), "NSE", "2025-03-28")
        self.assertEqual(f.close.iloc[0], 104)
        b = (
            UDIFF.replace(b",NSE,", b",BSE,")
            .replace(b",2885,", b",500325,")
            .replace(b",EQ,", b",A,")
        )
        f = parse_bhavcopy(b, "BSE", "2025-03-28")
        self.assertEqual(f.symbol.iloc[0], "500325")

    def test_bse_legacy(self):
        f = parse_bhavcopy(zipped(BSE), "BSE", "2020-01-01")
        self.assertEqual(f.symbol.iloc[0], "500325")

    def test_date_and_exchange_fail_closed(self):
        with self.assertRaisesRegex(InvalidData, "date"):
            parse_bhavcopy(UDIFF, "NSE", "2025-03-27")
        with self.assertRaisesRegex(InvalidData, "exchange"):
            parse_bhavcopy(UDIFF, "BSE", "2025-03-28")

    def test_unknown_schema_and_duplicate_rows(self):
        with self.assertRaises(InvalidData):
            parse_bhavcopy(b"foo,bar\n1,2", "NSE", "2025-03-28")
        with self.assertRaises(InvalidData):
            parse_bhavcopy(
                UDIFF + UDIFF.splitlines(keepends=True)[1], "NSE", "2025-03-28"
            )

    def test_zip_and_html_validation(self):
        for b in [
            b"PKbroken",
            b"<html>Access Denied</html>",
            zipped(UDIFF, "file.exe"),
        ]:
            with self.assertRaises(InvalidData):
                csv_bytes(b)

    def test_invalid_ohlc_is_visible_not_discarded(self):
        f = parse_bhavcopy(UDIFF.replace(b",105,99,", b",101,99,"), "NSE", "2025-03-28")
        self.assertEqual(f.quality_issue.iloc[0], "invalid_ohlc")

    def test_negative_and_fractional_volume(self):
        for v in [b"-1", b"1.5"]:
            with self.assertRaises(InvalidData):
                parse_bhavcopy(
                    UDIFF.replace(b",1000,", b"," + v + b","), "NSE", "2025-03-28"
                )

    def test_zero_volume_preserved(self):
        f = parse_bhavcopy(UDIFF.replace(b",1000,", b",0,"), "NSE", "2025-03-28")
        self.assertEqual(f.volume.iloc[0], 0)

    def test_nse_indices_turnover_units(self):
        b = b"Index Name,Index Date,Open Index Value,High Index Value,Low Index Value,Closing Index Value,Volume,Turnover (Rs. Cr.)\nNifty 50,28-03-2025,100,110,90,105,1000,2\n"
        f = parse_indices(b, "2025-03-28")
        self.assertEqual(f.turnover.iloc[0], 20_000_000)

    def test_transition_urls(self):
        self.assertIn("cm05JUL2024", archive_url("2024-07-05"))
        self.assertIn("BhavCopy_NSE_CM", archive_url("2024-07-08"))
        self.assertIn("EQ050724_CSV.ZIP", archive_url("2024-07-05", "BSE"))
        self.assertIn("BhavCopy_BSE_CM", archive_url("2024-07-08", "BSE"))


class FakeResponse:
    def __init__(self, body=b"ok", status=200, headers=None):
        self.body = body
        self.status_code = status
        self.headers = headers or {}

    def iter_content(self, n):
        yield self.body

    def close(self):
        pass


class FakeSession:
    def __init__(self, responses):
        self.headers = {}
        self.responses = list(responses)
        self.calls = []

    def get(self, url, **kwargs):
        self.calls.append(url)
        r = self.responses.pop(0)
        if isinstance(r, Exception):
            raise r
        return r


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.now = 1000.0

    def tearDown(self):
        self.tmp.cleanup()

    def sleep(self, n):
        self.now += n

    def client(self, responses, offline=False):
        self.session = FakeSession(responses)
        return Client(
            self.root,
            session=self.session,
            sleep=self.sleep,
            clock=lambda: self.now,
            offline=offline,
        )

    def test_cache_resume_and_offline(self):
        c = self.client([FakeResponse()])
        c.get("https://example.com/a", "raw/a")
        c.get("https://example.com/a", "raw/a")
        self.assertEqual(len(self.session.calls), 1)
        offline = self.client([], True)
        self.assertEqual(offline.get("https://example.com/a", "raw/a"), b"ok")
        (self.root / "raw/a").write_bytes(b"corrupt")
        with self.assertRaises(OfflineMiss):
            offline.get("https://example.com/a", "raw/a")

    def test_404_negative_cache(self):
        c = self.client([FakeResponse(status=404)])
        for _ in range(2):
            with self.assertRaises(MissingFile):
                c.get("https://example.com/a", "raw/a")
        self.assertEqual(len(self.session.calls), 1)

    def test_403_circuit_breaker_across_dates(self):
        c = self.client([FakeResponse(status=403)])
        with self.assertRaises(Blocked):
            c.get("https://example.com/a", "raw/a")
        with self.assertRaises(Blocked):
            c.get("https://example.com/b", "raw/b")
        self.assertEqual(len(self.session.calls), 1)

    def test_429_retry_after(self):
        c = self.client([FakeResponse(status=429, headers={"Retry-After": "900"})])
        with self.assertRaises(Blocked):
            c.get("https://example.com/a", "raw/a")
        state = json.loads((self.root / "cache/network.json").read_text())
        self.assertGreaterEqual(state["example.com"], 1900)

    def test_503_retry_after(self):
        c = self.client([FakeResponse(status=503, headers={"Retry-After": "900"})])
        with self.assertRaises(Blocked):
            c.get("https://example.com/a", "raw/a")
        self.assertGreaterEqual(
            json.loads((self.root / "cache/network.json").read_text())["example.com"],
            1900,
        )

    def test_age_based_refresh(self):
        c = self.client([FakeResponse(b"old"), FakeResponse(b"new")])
        c.get("https://example.com/a", "raw/a", max_age=10)
        self.now += 20
        self.assertEqual(c.get("https://example.com/a", "raw/a", max_age=10), b"new")

    def test_retry_and_rate_limit(self):
        c = self.client([FakeResponse(status=503), FakeResponse(), FakeResponse()])
        c.get("https://example.com/a", "raw/a")
        c.get("https://example.com/b", "raw/b")
        self.assertEqual(len(self.session.calls), 3)
        self.assertGreater(self.now, 1002)

    def test_invalid_download_not_cached(self):
        c = self.client([FakeResponse(b"<html>no</html>")])
        with self.assertRaises(InvalidData):
            c.get("https://example.com/a", "raw/a", csv_bytes)
        self.assertFalse((self.root / "raw/a").exists())

    def test_partial_download_retries(self):
        import requests

        c = self.client(
            [requests.exceptions.ChunkedEncodingError("partial"), FakeResponse()]
        )
        self.assertEqual(c.get("https://example.com/a", "raw/a"), b"ok")

    def test_path_traversal(self):
        with self.assertRaises(ValueError):
            self.client([]).get("https://example.com/a", "../escape")

    def test_downloader_continues_and_resumes(self):
        c = self.client([FakeResponse(status=404), FakeResponse(UDIFF)])
        store = IndiaData(client=c)
        r = store.download_bhavcopy("2025-03-27", "2025-03-29", calendar=Calendar())
        self.assertEqual(len(r.failures), 1)
        self.assertEqual(len(r.files), 1)
        r = store.download_bhavcopy("2025-03-28", "2025-03-29", calendar=Calendar())
        self.assertEqual(len(r.files), 1)
        self.assertEqual(len(self.session.calls), 2)

    def test_holiday_skips_requests(self):
        c = self.client([])
        r = IndiaData(client=c).download_bhavcopy(
            "2025-03-29", "2025-04-01", calendar=Calendar({"2025-03-31"})
        )
        self.assertEqual(r.sessions, [])
        self.assertEqual(len(self.session.calls), 0)

    def test_calendar_year_validated(self):
        data = {"CM": [{"tradingDate": f"{d:02}-Jan-2025"} for d in range(1, 6)]}
        c = self.client([FakeResponse(json.dumps(data).encode())])
        with self.assertRaises(InvalidData):
            Calendar.load(c, "2024-01-01", "2024-02-01")

    def test_bse_explicit_calendar_required(self):
        with self.assertRaisesRegex(ValueError, "BSE requires"):
            Calendar.load(self.client([]), "2025-01-01", "2025-02-01", exchange="BSE")

    def test_bse_constituents(self):
        c = self.client(
            [FakeResponse(json.dumps({"Table": [{"SCRIP_CODE": "500325"}]}).encode())]
        )
        self.assertEqual(constituents(c, "SENSEX").symbol.tolist(), ["500325"])


class MasterTests(unittest.TestCase):
    def test_nse_and_sme_headers(self):
        for head in [
            "NAME OF COMPANY, SERIES, DATE OF LISTING, ISIN NUMBER",
            "NAME_OF_COMPANY,SERIES,DATE_OF_LISTING,ISIN_NUMBER",
        ]:
            f = master_csv(
                (
                    "SYMBOL,"
                    + head
                    + "\nRELIANCE,Reliance,EQ,01-Jan-1995,INE002A01018\n"
                ).encode(),
                "NSE",
            )
            self.assertEqual(f.yahoo_ticker.iloc[0], "RELIANCE.NS")
            self.assertTrue(f.is_active.iloc[0])

    def test_dual_listing_preference_and_empty_isin(self):
        f = pd.DataFrame(
            [
                ["RELIANCE", "NSE", "INE002A01018"],
                ["500325", "BSE", "INE002A01018"],
                ["1", "BSE", ""],
                ["2", "BSE", ""],
            ],
            columns=["symbol", "exchange", "isin"],
        )
        self.assertEqual(set(prefer_exchange(f).symbol), {"RELIANCE", "1", "2"})
        self.assertIn("500325", set(prefer_exchange(f, "BSE").symbol))


if __name__ == "__main__":
    unittest.main()
