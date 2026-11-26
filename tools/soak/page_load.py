"""A page load as a browser makes it (spec 045), for ws_soak.py.

index.html, then the files it names, up to BROWSER_CONNECTIONS at once. With a
cache, the hashed /assets/ files are fetched once (the device sends them with
Cache-Control immutable). Standard library only.
"""

from __future__ import annotations

import re
import threading
import urllib.error
from collections.abc import Callable

# A browser opens up to 6 connections to one host.
BROWSER_CONNECTIONS = 6

Get = Callable[..., tuple[int, bytes]]


def page_assets(index_html: str) -> list[str]:
    """The files (scripts, styles, icons) a page's index.html loads, in order, once each."""
    found: list[str] = []
    for path in re.findall(r'(?:src|href)="(/[^"#?]+)"', index_html):
        if path not in found and path != "/":
            found.append(path)
    return found


def _fetch(get: Get, path: str, results: list[str], cached: set[str] | None) -> None:
    try:
        code, _ = get(path, timeout=20)
    except (OSError, urllib.error.URLError):
        results.append("failed")
        return
    results.append("ok" if code == 200 else "busy" if code == 503 else "failed")
    if code == 200 and cached is not None and path.startswith("/assets/"):
        cached.add(path)


def fetch_page(get: Get, cached: set[str] | None = None) -> tuple[bool, int]:
    """(every file came, files fetched). `cached`: the /assets/ files kept since an earlier load."""
    try:
        code, body = get("/")
    except (OSError, urllib.error.URLError):
        return False, 0
    if code != 200:
        return False, 0
    wanted = [p for p in page_assets(body.decode("utf-8", "replace")) if cached is None or p not in cached]
    results: list[str] = []
    for i in range(0, len(wanted), BROWSER_CONNECTIONS):
        batch = [threading.Thread(target=_fetch, args=(get, p, results, cached), daemon=True) for p in wanted[i : i + BROWSER_CONNECTIONS]]
        for t in batch:
            t.start()
        for t in batch:
            t.join()
    return all(r == "ok" for r in results), len(results)
