"""Versioned official symbol and constituent snapshots. Never infer historical membership."""

from __future__ import annotations
import json
from pathlib import Path
import pandas as pd
from .parsers import read_csv, require
from .transport import InvalidData, atomic_write, sha

MASTER_FIELDS = [
    "symbol",
    "exchange",
    "isin",
    "name",
    "series",
    "is_active",
    "listing_date",
    "yahoo_ticker",
]
NIFTY = {
    "NIFTY50": "ind_nifty50list.csv",
    "NIFTYNEXT50": "ind_niftynext50list.csv",
    "NIFTY100": "ind_nifty100list.csv",
    "NIFTY200": "ind_nifty200list.csv",
    "NIFTY500": "ind_nifty500list.csv",
}
# BSE public constituent JSON is configured below from its official index site.
BSE_INDICES = {"SENSEX": "16", "BSE100": "22", "BSE500": "17"}


def master_csv(body, exchange):
    f = read_csv(body)
    if set(MASTER_FIELDS).issubset(set(c.lower() for c in f.columns)):
        f.columns = f.columns.str.lower()
        out = f[f.exchange.eq(exchange)].copy()
    elif exchange == "NSE":
        require(
            f, ["SYMBOL", "NAME_OF_COMPANY", "SERIES", "DATE_OF_LISTING", "ISIN_NUMBER"]
        )
        out = f.rename(
            columns={
                "SYMBOL": "symbol",
                "NAME_OF_COMPANY": "name",
                "SERIES": "series",
                "DATE_OF_LISTING": "listing_date",
                "ISIN_NUMBER": "isin",
            }
        )
        out["exchange"] = "NSE"
        out["is_active"] = True
        out["yahoo_ticker"] = out.symbol + ".NS"
        out["listing_date"] = pd.to_datetime(
            out.listing_date, format="mixed", dayfirst=True, errors="raise"
        ).dt.strftime("%Y-%m-%d")
    else:
        # Official BSE website's downloaded List of Securities CSV.
        require(f, ["SECURITY_CODE", "SECURITY_NAME", "ISIN_NO", "GROUP"])
        out = f.rename(
            columns={
                "SECURITY_CODE": "symbol",
                "SECURITY_NAME": "name",
                "ISIN_NO": "isin",
                "GROUP": "series",
            }
        )
        out["exchange"] = "BSE"
        out["is_active"] = f.get("STATUS", pd.Series("Active", index=f.index)).eq(
            "Active"
        )
        out["listing_date"] = ""
        out["yahoo_ticker"] = (
            ""  # Vendor mapping is unknown; numeric BSE codes are not Yahoo tickers.
        )
    require(out, MASTER_FIELDS)
    out = out[
        MASTER_FIELDS
        + (
            ["market_cap_inr", "market_cap_asof"]
            if {"market_cap_inr", "market_cap_asof"}.issubset(out)
            else []
        )
    ].copy()
    out["is_active"] = (
        out.is_active.astype(str)
        .str.lower()
        .map({"true": True, "false": False, "1": True, "0": False})
    )
    if (
        out.empty
        or out.symbol.eq("").any()
        or out.is_active.isna().any()
        or not out.exchange.eq(exchange).all()
        or out.duplicated(["symbol", "series"]).any()
    ):
        raise InvalidData("invalid symbol master")
    if exchange == "BSE" and not out.symbol.str.fullmatch(r"\d{6}").all():
        raise InvalidData("BSE master requires scrip codes")
    out["replay_symbol"] = out.symbol + (".NS" if exchange == "NSE" else ".BO")
    return out.sort_values(["symbol", "series"]).reset_index(drop=True)


def bse_master_json(body):
    try:
        payload = json.loads(body)
        f = pd.DataFrame(
            payload.get("Table", payload) if isinstance(payload, dict) else payload
        )
        f = f.rename(
            columns={
                "SCRIP_CD": "symbol",
                "scrip_cd": "symbol",
                "Scrip_Name": "name",
                "scrip_name": "name",
                "ISIN_NUMBER": "isin",
                "GROUP": "series",
                "Status": "status",
            }
        )
        require(f, ["symbol", "name", "isin", "series"])
        f["symbol"] = f.symbol.astype(str)
        f["exchange"] = "BSE"
        f["listing_date"] = ""
        f["is_active"] = True
        f["yahoo_ticker"] = ""
        return master_csv(f[MASTER_FIELDS].to_csv(index=False).encode(), "BSE")
    except (ValueError, TypeError) as exc:
        raise InvalidData(f"invalid BSE master JSON: {exc}") from exc


def get_symbol_master(
    client,
    exchange="NSE",
    *,
    refresh=False,
    include_sme=False,
    master_file=None,
    max_age_days=None,
):
    exchange = exchange.upper()
    if max_age_days is not None and (
        not isinstance(max_age_days, (int, float)) or not 0 < max_age_days <= 3650
    ):
        raise ValueError("max_age_days must be in (0,3650]")
    max_age = None if max_age_days is None else max_age_days * 86400
    if exchange not in ("NSE", "BSE"):
        raise ValueError("unknown exchange")
    if master_file:
        result = master_csv(Path(master_file).read_bytes(), exchange)
    elif exchange == "NSE":
        urls = [
            (
                "mainboard",
                "https://nsearchives.nseindia.com/content/equities/EQUITY_L.csv",
            )
        ]
        if include_sme:
            urls.append(
                (
                    "sme",
                    "https://nsearchives.nseindia.com/emerge/corporates/content/SME_EQUITY_L.csv",
                )
            )
        parts = []
        for segment, url in urls:
            body = client.get(
                url,
                f"masters/raw/nse-{segment}.csv",
                lambda b: master_csv(b, exchange),
                refresh=refresh,
                max_age=max_age,
            )
            parts.append(master_csv(body, exchange))
        result = (
            pd.concat(parts)
            .drop_duplicates(["symbol", "series"], keep="first")
            .sort_values(["symbol", "series"])
        )
    else:
        url = "https://api.bseindia.com/BseIndiaAPI/api/ListofScripData/w?Group=&Scripcode=&industry=&segment=Equity&status=Active"
        body = client.get(
            url,
            "masters/raw/bse.json",
            bse_master_json,
            refresh=refresh,
            max_age=max_age,
        )
        result = bse_master_json(body)
    content = result.to_csv(index=False).encode()
    # Retain all versions; a refresh never destroys the previous universe snapshot.
    atomic_write(
        client.root / f"masters/{exchange.lower()}-{sha(content)[:16]}.csv", content
    )
    return result.reset_index(drop=True)


def constituents(client, universe, *, refresh=False, universe_file=None):
    name = universe.upper().replace(" ", "").replace("_", "")
    if universe_file:
        frame = pd.read_csv(universe_file, dtype=str, keep_default_na=False)
        if "symbol" not in frame:
            raise InvalidData("universe_file needs a symbol column")
        if frame.symbol.eq("").any() or frame.symbol.duplicated().any():
            raise InvalidData("invalid constituent list")
        return frame
    if name in NIFTY:

        def parse(b):
            f = read_csv(b)
            require(f, ["SYMBOL", "ISIN_CODE", "SERIES"])
            if f.SYMBOL.duplicated().any() or f.SYMBOL.eq("").any():
                raise InvalidData("invalid constituents")
            return f.rename(
                columns={"SYMBOL": "symbol", "ISIN_CODE": "isin", "SERIES": "series"}
            )[["symbol", "isin", "series"]]

        url = f"https://www.niftyindices.com/IndexConstituent/{NIFTY[name]}"
        body = client.get(url, f"masters/universes/{name}.csv", parse, refresh=refresh)
        frame = parse(body)
        atomic_write(
            client.root / f"masters/universes/{name}-{sha(body)[:16]}.csv",
            frame.to_csv(index=False).encode(),
        )
        return frame
    if name in BSE_INDICES:

        def parse(b):
            try:
                f = pd.DataFrame(json.loads(b)["Table"])
                require(f, ["SCRIP_CODE"])
                f = f.rename(columns={"SCRIP_CODE": "symbol"})
                f["symbol"] = f.symbol.astype(str)
                if (
                    f.empty
                    or not f.symbol.str.fullmatch(r"\d{6}").all()
                    or f.symbol.duplicated().any()
                ):
                    raise InvalidData("invalid BSE constituents")
                return f[["symbol"]]
            except (KeyError, ValueError, TypeError) as exc:
                raise InvalidData(f"invalid BSE constituents: {exc}") from exc

        url = f"https://www.bseindices.com/AsiaIndexAPI/api/Codewise_Indices/w?code={BSE_INDICES[name]}"
        body = client.get(url, f"masters/universes/{name}.json", parse, refresh=refresh)
        frame = parse(body)
        atomic_write(
            client.root / f"masters/universes/{name}-{sha(body)[:16]}.csv",
            frame.to_csv(index=False).encode(),
        )
        return frame
    raise ValueError(f"unknown universe {universe}")


def prefer_exchange(master, preference="NSE"):
    """ISIN-based deduplication only; blank ISINs never collapse unrelated stocks."""
    if preference not in ("NSE", "BSE"):
        raise ValueError("exchange preference must be NSE or BSE")
    f = master.copy()
    f["priority"] = f.exchange.ne(preference).astype(int)
    f["key"] = f["isin"].where(f["isin"].ne(""), f.exchange + ":" + f.symbol)
    return (
        f.sort_values(["priority", "exchange", "symbol"], kind="stable")
        .drop_duplicates("key")
        .drop(columns=["key", "priority"])
    )
