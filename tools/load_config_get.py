#!/usr/bin/env python3
"""Hammer a RoofRoof's GET /api/config (spec 029 research R13) and say whether it restarted.

Several clients read the settings at once, as several open pages do when the
settings revision changes. Before the fix this aborted the device within
minutes. Pass: the device answered throughout and never restarted (its uptime
only grew and its boot count stayed). Read-only: nothing is saved or moved.

    python3 tools/load_config_get.py 192.168.1.245 --clients 6 --minutes 5
    python3 tools/load_config_get.py roofroof.local -u admin:PASSWORD --also /api/status /api/diag

Exit status 0 on a pass, 1 when the device restarted, 2 when it couldn't be reached.
"""

from __future__ import annotations

import argparse
import base64
import json
import sys
import threading
import time
import urllib.error
import urllib.request
from collections import Counter

STATUS = "/api/status"


def fetch(base: str, path: str, auth: str | None, timeout: float) -> tuple[int, bytes]:
    request = urllib.request.Request(base + path)
    if auth:
        request.add_header("Authorization", "Basic " + base64.b64encode(auth.encode()).decode())
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, b""


def boot_of(base: str, auth: str | None) -> tuple[int, int, int] | None:
    """(uptime s, boot count, reset reason) or None while it doesn't answer."""
    try:
        status, body = fetch(base, STATUS, auth, 5)
        doc = json.loads(body) if status == 200 else None
    except (OSError, ValueError):
        return None
    if not doc:
        return None
    return int(doc.get("uptime", 0)), int(doc.get("bootCount", 0)), int(doc.get("resetReason", -1))


class Load:
    def __init__(self, base: str, paths: list[str], auth: str | None):
        self.base, self.paths, self.auth = base, paths, auth
        self.results: Counter[str] = Counter()
        self.lock = threading.Lock()
        self.stop = threading.Event()

    def client(self, index: int) -> None:
        n = index
        while not self.stop.is_set():
            path = self.paths[n % len(self.paths)]
            n += 1
            try:
                status, _ = fetch(self.base, path, self.auth, 10)
                key = f"{path} {status}"
            except OSError:
                key = f"{path} no answer"
            with self.lock:
                self.results[key] += 1


def watch(base: str, auth: str | None, seconds: float, start: tuple[int, int, int]) -> list[str]:
    """Polls the uptime; returns what looked like restarts."""
    restarts = []
    last = start
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        time.sleep(5)
        now = boot_of(base, auth)
        if now is None:
            continue
        if now[0] < last[0] or now[1] != last[1]:
            restarts.append(f"restarted: uptime {last[0]} s -> {now[0]} s, reset reason {now[2]}")
        last = now
    return restarts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("host", help="the device's address, e.g. 192.168.1.245")
    parser.add_argument("--clients", type=int, default=6)
    parser.add_argument("--minutes", type=float, default=5)
    parser.add_argument("-u", "--user", help="user:password when protection is on")
    parser.add_argument("--also", nargs="*", default=[], help="more paths to read in the same loop")
    args = parser.parse_args(argv)
    base = args.host if args.host.startswith("http") else "http://" + args.host
    start = boot_of(base, args.user)
    if start is None:
        print(f"{base}{STATUS} doesn't answer", file=sys.stderr)
        return 2
    print(f"start: uptime {start[0]} s, boot {start[1]}; {args.clients} clients for {args.minutes} min")
    load = Load(base, ["/api/config", *args.also], args.user)
    threads = [threading.Thread(target=load.client, args=(i,), daemon=True) for i in range(args.clients)]
    for thread in threads:
        thread.start()
    restarts = watch(base, args.user, args.minutes * 60, start)
    load.stop.set()
    for thread in threads:
        thread.join(timeout=15)
    for key, count in sorted(load.results.items()):
        print(f"  {key}: {count}")
    if restarts:
        print("FAIL: " + "; ".join(restarts) + " (GET /api/diag/crashes has the record)")
        return 1
    print("PASS: no restart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
