"""Schema-based parsers for official legacy and UDiFF cash-market archives."""

from __future__ import annotations
import io
import zipfile
import numpy as np
import pandas as pd
from .transport import InvalidData

FIELDS = [
    "date",
    "symbol",
    "exchange",
    "isin",
    "name",
    "series",
    "open",
    "high",
    "low",
    "close",
    "volume",
    "turnover",
    "deliverable_qty",
    "quality_issue",
]


def csv_bytes(body: bytes) -> bytes:
    if body.startswith(b"PK"):
        try:
            with zipfile.ZipFile(io.BytesIO(body)) as archive:
                files = [f for f in archive.infolist() if not f.is_dir()]
                if (
                    len(files) != 1
                    or not files[0].filename.lower().endswith(".csv")
                    or files[0].file_size > 100 * 1024 * 1024
                ):
                    raise InvalidData("expected one CSV member <=100 MiB")
                body = archive.read(
                    files[0]
                )  # CRC checked; never extract filesystem paths
        except (zipfile.BadZipFile, RuntimeError, EOFError) as exc:
            raise InvalidData(f"invalid ZIP: {exc}") from exc
    if b"<html" in body[:1024].lower() or b"<!doctype" in body[:1024].lower():
        raise InvalidData("HTML returned instead of data")
    return body


def read_csv(body):
    try:
        frame = pd.read_csv(
            io.BytesIO(csv_bytes(body)),
            dtype=str,
            encoding="utf-8-sig",
            keep_default_na=False,
        )
    except (pd.errors.ParserError, pd.errors.EmptyDataError, UnicodeError) as exc:
        raise InvalidData(f"invalid CSV: {exc}") from exc
    frame.columns = [c.strip().upper().replace(" ", "_") for c in frame.columns]
    if frame.empty or frame.columns.duplicated().any():
        raise InvalidData("empty or duplicate-column CSV")
    return frame.apply(lambda c: c.str.strip())


def require(frame, columns):
    missing = set(columns) - set(frame)
    if missing:
        raise InvalidData(f"unrecognized schema; missing {sorted(missing)}")


def parse_bhavcopy(body, exchange, day):
    exchange = exchange.upper()
    if exchange not in ("NSE", "BSE"):
        raise ValueError("exchange must be NSE or BSE")
    f = read_csv(body)
    if "TRADDT" in f:
        require(
            f,
            [
                "TRADDT",
                "SGMT",
                "SRC",
                "FININSTRMTP",
                "FININSTRMID",
                "TCKRSYMB",
                "SCTYSRS",
                "ISIN",
                "OPNPRIC",
                "HGHPRIC",
                "LWPRIC",
                "CLSPRIC",
                "TTLTRADGVOL",
            ],
        )
        if not f.SRC.str.upper().eq(exchange).all() or not f.SGMT.eq("CM").all():
            raise InvalidData("wrong exchange or segment")
        if (
            not pd.to_datetime(f.TRADDT, format="%Y-%m-%d", errors="coerce")
            .eq(pd.Timestamp(day))
            .all()
        ):
            raise InvalidData("archive date mismatch")
        f = f[f.FININSTRMTP.eq("STK")].copy()
        mapping = {
            "TRADDT": "date",
            "TCKRSYMB": "symbol",
            "ISIN": "isin",
            "FININSTRMNM": "name",
            "SCTYSRS": "series",
            "OPNPRIC": "open",
            "HGHPRIC": "high",
            "LWPRIC": "low",
            "CLSPRIC": "close",
            "TTLTRADGVOL": "volume",
            "TTLTRFVAL": "turnover",
        }
        if exchange == "BSE":
            mapping.update({"FININSTRMID": "symbol", "TCKRSYMB": "ticker"})
    elif exchange == "NSE":
        require(
            f,
            [
                "SYMBOL",
                "SERIES",
                "OPEN",
                "HIGH",
                "LOW",
                "CLOSE",
                "TOTTRDQTY",
                "TIMESTAMP",
            ],
        )
        dates = pd.to_datetime(f.TIMESTAMP, format="%d-%b-%Y", errors="coerce")
        if not dates.eq(pd.Timestamp(day)).all():
            raise InvalidData("archive date mismatch")
        mapping = {
            "SYMBOL": "symbol",
            "SERIES": "series",
            "OPEN": "open",
            "HIGH": "high",
            "LOW": "low",
            "CLOSE": "close",
            "TOTTRDQTY": "volume",
            "TOTTRDVAL": "turnover",
            "ISIN": "isin",
        }
    else:
        require(
            f,
            [
                "SC_CODE",
                "SC_NAME",
                "SC_GROUP",
                "SC_TYPE",
                "OPEN",
                "HIGH",
                "LOW",
                "CLOSE",
                "NO_OF_SHRS",
            ],
        )
        f = f[f.SC_TYPE.eq("Q")].copy()
        mapping = {
            "SC_CODE": "symbol",
            "SC_NAME": "name",
            "SC_GROUP": "series",
            "OPEN": "open",
            "HIGH": "high",
            "LOW": "low",
            "CLOSE": "close",
            "NO_OF_SHRS": "volume",
            "NET_TURNOV": "turnover",
            "ISIN_CODE": "isin",
        }
        # Legacy BSE has no date column: the validated download URL supplies it.
    out = f.rename(columns=mapping)
    out["date"] = pd.Timestamp(day).strftime("%Y-%m-%d")
    out["exchange"] = exchange
    if "DELIV_QTY" in out:
        out["deliverable_qty"] = out.DELIV_QTY
    for col in FIELDS:
        if col not in out:
            out[col] = "" if col in ["isin", "name", "series"] else np.nan
    out = out[FIELDS].copy()
    if out.empty:
        raise InvalidData("no cash equity records")
    if (
        out.symbol.eq("").any()
        or out.series.eq("").any()
        or out.duplicated(["symbol", "series"]).any()
    ):
        raise InvalidData("empty identifiers or duplicate symbol/series")
    if exchange == "BSE" and not out.symbol.str.fullmatch(r"\d{6}").all():
        raise InvalidData("BSE symbols must be six-digit scrip codes")
    for col in [
        "open",
        "high",
        "low",
        "close",
        "volume",
        "turnover",
        "deliverable_qty",
    ]:
        original = out[col].replace({"": np.nan, "-": np.nan})
        out[col] = pd.to_numeric(original, errors="coerce")
        required = col in ["open", "high", "low", "close", "volume"]
        if (
            (required and out[col].isna().any())
            or (original.notna() & out[col].isna()).any()
            or np.isinf(out[col]).any()
            or (out[col] < 0).any()
        ):
            raise InvalidData(f"invalid numeric {col}")
    if (
        not (out.volume == np.floor(out.volume)).all()
        or (out.volume > 1_000_000_000_000).any()
    ):
        raise InvalidData("fractional or out-of-range volume")
    invalid = (
        (out[["open", "high", "low", "close"]] <= 0).any(axis=1)
        | (out.low > out[["open", "close"]].min(axis=1))
        | (out.high < out[["open", "close"]].max(axis=1))
    )
    out["quality_issue"] = np.where(invalid, "invalid_ohlc", "")
    out["volume"] = out.volume.astype("int64")
    return out.sort_values(["symbol", "series"], kind="stable").reset_index(drop=True)


def parse_indices(body, day):
    f = read_csv(body)
    require(
        f,
        [
            "INDEX_NAME",
            "INDEX_DATE",
            "OPEN_INDEX_VALUE",
            "HIGH_INDEX_VALUE",
            "LOW_INDEX_VALUE",
            "CLOSING_INDEX_VALUE",
            "VOLUME",
        ],
    )
    if (
        not pd.to_datetime(f.INDEX_DATE, format="%d-%m-%Y", errors="coerce")
        .eq(pd.Timestamp(day))
        .all()
    ):
        raise InvalidData("index date mismatch")
    out = f.rename(
        columns={
            "INDEX_NAME": "symbol",
            "OPEN_INDEX_VALUE": "open",
            "HIGH_INDEX_VALUE": "high",
            "LOW_INDEX_VALUE": "low",
            "CLOSING_INDEX_VALUE": "close",
            "VOLUME": "volume",
            "TURNOVER_(RS._CR.)": "turnover",
        }
    )
    out["date"] = str(day)
    out["exchange"] = "NSE"
    out["series"] = "INDEX"
    out["isin"] = ""
    out["name"] = out.symbol
    out["deliverable_qty"] = np.nan
    for col in ["open", "high", "low", "close", "volume", "turnover"]:
        out[col] = pd.to_numeric(
            out.get(col, pd.Series(np.nan, index=out.index)).replace(
                {"-": np.nan, "": np.nan}
            ),
            errors="coerce",
        )
    out["turnover"] *= 10_000_000  # official index file expresses INR crores
    if out.symbol.duplicated().any() or out.close.isna().any() or (out.close < 0).any():
        raise InvalidData("invalid index records")
    out["quality_issue"] = ""
    return out[FIELDS].sort_values("symbol").reset_index(drop=True)
