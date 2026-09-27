"""Official Indian market archives; public API dates use an exclusive end."""

from .archive import IndiaData
from .transport import Client, DownloadError, InvalidData

__all__ = [
    "IndiaData",
    "Client",
    "DownloadError",
    "InvalidData",
    "get_symbol_master",
    "download_bhavcopy",
    "load_ohlcv",
]


def get_symbol_master(
    exchange="NSE", *, root="backtesting/cache/india", offline=False, **kwargs
):
    return IndiaData(root, offline=offline).get_symbol_master(exchange, **kwargs)


def download_bhavcopy(
    start_date,
    end_date,
    exchange="NSE",
    *,
    root="backtesting/cache/india",
    offline=False,
    **kwargs,
):
    return IndiaData(root, offline=offline).download_bhavcopy(
        start_date, end_date, exchange, **kwargs
    )


def load_ohlcv(
    symbol,
    start,
    end,
    adjusted=False,
    *,
    root="backtesting/cache/india",
    offline=False,
    **kwargs,
):
    return IndiaData(root, offline=offline).load_ohlcv(
        symbol, start, end, adjusted, **kwargs
    )
