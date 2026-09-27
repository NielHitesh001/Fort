"""Polite public-file transport. Cache-wide locks coordinate processes as well as threads."""

from __future__ import annotations
from contextlib import contextmanager
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
import fcntl
import hashlib
import json
import logging
import os
from pathlib import Path
import random
import tempfile
import time
from urllib.parse import urlparse
import requests

LOG = logging.getLogger("fort.india")
MAX_DOWNLOAD = 32 * 1024 * 1024


class DownloadError(RuntimeError):
    pass


class MissingFile(DownloadError):
    pass


class Blocked(DownloadError):
    pass


class OfflineMiss(DownloadError):
    pass


class InvalidData(ValueError):
    pass


def sha(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".partial-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as file:
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


@contextmanager
def locked(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as file:
        fcntl.flock(file, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(file, fcntl.LOCK_UN)


class Client:
    def __init__(
        self,
        root,
        *,
        offline=False,
        interval=1.05,
        attempts=3,
        session=None,
        sleep=time.sleep,
        clock=time.time,
    ):
        if interval < 0.5 or not 1 <= attempts <= 5:
            raise ValueError("request interval must be >=0.5s; attempts 1..5")
        self.root = Path(root).resolve()
        self.offline = offline
        self.interval, self.attempts = interval, attempts
        self.session = session or requests.Session()
        self.session.headers.update(
            {
                "User-Agent": "Fort-Backtest/1.0 (public EOD archives; personal research)",
                "Accept": "text/csv, application/zip, application/json, */*",
            }
        )
        self.sleep, self.clock = sleep, clock

    def _record(self, **entry):
        entry["at"] = datetime.fromtimestamp(self.clock(), timezone.utc).isoformat()
        LOG.info("%s", entry)
        with locked(self.root / "cache/log.lock"):
            path = self.root / "downloads.jsonl"
            with path.open("a") as out:
                out.write(json.dumps(entry, sort_keys=True) + "\n")

    def get(
        self, url, relative, validate=lambda b: None, *, refresh=False, max_age=None
    ):
        path = (self.root / relative).resolve()
        if not path.is_relative_to(self.root):
            raise ValueError("cache path escapes root")
        if not url.startswith("https://"):
            raise ValueError("HTTPS required")
        key = sha(str(path).encode())
        meta_path = path.with_name(path.name + ".json")
        with locked(self.root / "cache" / (key + ".lock")):
            try:
                meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
            except (ValueError, OSError):
                meta = {}
            valid_cache = (
                path.exists()
                and meta.get("url") == url
                and sha(path.read_bytes()) == meta.get("sha256")
            )
            if valid_cache and (
                self.offline
                or (
                    not refresh
                    and (
                        max_age is None
                        or self.clock() - meta.get("fetched_at", 0) < max_age
                    )
                )
            ):
                body = path.read_bytes()
                validate(body)
                self._record(url=url, status="cache_hit")
                return body
            if self.offline:
                raise OfflineMiss(f"uncached/corrupt offline resource: {path}")
            if (
                not refresh
                and meta.get("url") == url
                and meta.get("missing_until", 0) > self.clock()
            ):
                raise MissingFile(f"cached missing archive: {url}")
            host = urlparse(url).netloc
            for attempt in range(self.attempts):
                # Hold this lock throughout a request. Download concurrency is
                # deliberately one per cache; multiple collectors share the cap.
                with locked(self.root / "cache/network.lock"):
                    rate_file = self.root / "cache/network.json"
                    state = (
                        json.loads(rate_file.read_text()) if rate_file.exists() else {}
                    )
                    if state.get(host, 0) > self.clock():
                        raise Blocked(f"cooldown for {host} until {state[host]}")
                    delay = max(0, state.get("next", 0) - self.clock())
                    if delay:
                        self.sleep(delay)
                    self._record(url=url, status="attempt", attempt=attempt + 1)
                    response = None
                    retry = False
                    retry_wait = 0.0
                    try:
                        response = self.session.get(
                            url, timeout=(10, 30), stream=True, allow_redirects=False
                        )
                        status = response.status_code
                        self._record(url=url, status=status, attempt=attempt + 1)
                        if status in (403, 429):
                            wait = 86400 if status == 403 else 300
                            header = response.headers.get("Retry-After", "")
                            try:
                                wait = max(wait, float(header))
                            except ValueError:
                                try:
                                    wait = max(
                                        wait,
                                        parsedate_to_datetime(header).timestamp()
                                        - self.clock(),
                                    )
                                except (TypeError, ValueError, OverflowError):
                                    pass
                            state[host] = self.clock() + wait
                            raise Blocked(
                                f"HTTP {status}; honoring {wait:.0f}s cooldown for {host}"
                            )
                        if status in (404, 410):
                            atomic_write(
                                meta_path,
                                json.dumps(
                                    {
                                        **meta,
                                        "url": url,
                                        "missing_until": self.clock() + 43200,
                                    }
                                ).encode(),
                            )
                            raise MissingFile(f"HTTP {status}: {url}")
                        if status >= 500:
                            header = response.headers.get("Retry-After", "")
                            try:
                                retry_wait = max(0.0, float(header))
                            except ValueError:
                                try:
                                    retry_wait = max(
                                        0.0,
                                        parsedate_to_datetime(header).timestamp()
                                        - self.clock(),
                                    )
                                except (TypeError, ValueError, OverflowError):
                                    pass
                            if retry_wait > 60:
                                state[host] = self.clock() + retry_wait
                                raise Blocked(
                                    f"HTTP {status}; honoring Retry-After for {host}"
                                )
                            retry = True
                            raise DownloadError(f"HTTP {status}: {url}")
                        if status != 200:
                            raise DownloadError(f"HTTP {status}: {url}")
                        chunks, size = [], 0
                        for chunk in response.iter_content(65536):
                            size += len(chunk)
                            if size > MAX_DOWNLOAD:
                                raise InvalidData("download exceeds 32 MiB")
                            chunks.append(chunk)
                        body = b"".join(chunks)
                        if not body:
                            raise InvalidData("empty response")
                        # Only validated complete files enter the cache.
                        validate(body)
                        atomic_write(path, body)
                        atomic_write(
                            meta_path,
                            json.dumps(
                                {
                                    "url": url,
                                    "sha256": sha(body),
                                    "fetched_at": self.clock(),
                                }
                            ).encode(),
                        )
                        self._record(
                            url=url, status="stored", bytes=len(body), sha256=sha(body)
                        )
                        return body
                    except requests.RequestException as exc:
                        retry = True
                        error = DownloadError(f"transport error for {url}: {exc}")
                        self._record(url=url, status="network_error", error=str(exc))
                    except (InvalidData, DownloadError) as exc:
                        self._record(url=url, status="failure", error=str(exc))
                        if not retry:
                            raise
                        error = exc
                    finally:
                        if response is not None:
                            response.close()
                        state["next"] = (
                            self.clock() + self.interval + random.uniform(0, 0.15)
                        )
                        atomic_write(rate_file, json.dumps(state).encode())
                if attempt + 1 < self.attempts:
                    self.sleep(max(retry_wait, 2**attempt + random.uniform(0, 0.2)))
            raise error
