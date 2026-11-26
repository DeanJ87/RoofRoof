#!/usr/bin/env python3
"""Network-return soak (spec 048): cut the device's network N times while it is polled, check the heap.

Each cycle runs --cut (a shell command that takes the network away: an access
point's switch port down, a firewall rule), waits --down seconds, runs
--restore, and leaves the network up --up seconds. All along, the paths in
--also are polled like a harness and an imaging app (GET /api/status at 1 Hz by
default), and each /api/status answer gives the heap figures. After the last
cycle (once its return's minute of follow-up is over) GET /api/diag/heap is read
once: its "Network returns" section lists every return the device saw, with the
lowest largest block and free heap it sampled ten times a second over the minute
after each, the connections it answered busy and when MQTT came back.

    # The shakedown's access point (switch port 4) down 90 s, five times:
    python3 tools/soak/return_soak.py --host 192.168.1.245 --cycles 5 --down 90 --up 120 \\
        --cut "python3 shakedown/switch.py --port 4 --off" --restore "python3 shakedown/switch.py --port 4 --on" \\
        --also /api/status@1 "/api/v1/dome/0/shutterstatus?ClientID=1&ClientTransactionID=1@1"

Pass (spec 048 SC-001): no restart (uptime never goes back), the emergency
reserve never spent (heap8Bit.reserveSpent unchanged), the device recorded at
least --cycles returns, and the lowest largest block after each of them was at
or above --floor bytes (16 KB), in its own record and in every status sample.
Exit status 0 on a pass, 1 on a fail, 2 when the device couldn't be reached at
the start. Only the Python standard library is used; GET only, nothing is saved
or moved (the cut and restore commands are the caller's).
"""

from __future__ import annotations

import argparse
import base64
import json
import re
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

# The device's census can't be asked more often than this (it walks the heap).
DIAG_MIN_GAP_S = 30
# A return's record is complete this long after it (ConnectionBudget::RETURN_FOLLOW_MS).
FOLLOW_S = 60

RETURN_LINE = re.compile(
    r"minute\s+(?P<minute>\d+), uptime (?P<uptime>\d+) s: (?P<kind>[^;]+); "
    r"(?:lowest largest (?P<largest>\d+) B, free (?P<free>\d+) B|no heap reading yet); "
    r"(?P<refused>\d+) busy at connect, at most (?P<live>\d+) open; "
    r"(?:MQTT after (?P<mqtt>[\d.]+) s|MQTT not connected since); (?P<discovery>.+)$"
)
RETURNS_HEADER = re.compile(r"Network returns \(newest first; (?P<count>\d+) since start")


def parse_returns(text: str) -> tuple[int | None, list[dict]]:
    """The census's return count and its return lines, newest first."""
    count = None
    returns: list[dict] = []
    for line in text.splitlines():
        header = RETURNS_HEADER.search(line)
        if header:
            count = int(header.group("count"))
            continue
        m = RETURN_LINE.search(line)
        if not m:
            continue
        returns.append(
            {
                "uptime": int(m.group("uptime")),
                "kind": m.group("kind"),
                "largest": int(m.group("largest")) if m.group("largest") else None,
                "free": int(m.group("free")) if m.group("free") else None,
                "refused": int(m.group("refused")),
                "live": int(m.group("live")),
                "mqtt": float(m.group("mqtt")) if m.group("mqtt") else None,
                "discovery": m.group("discovery"),
            }
        )
    return count, returns


def split_poll(spec: str, default: float) -> tuple[str, float]:
    path, _, every = spec.rpartition("@")
    if not path:
        return spec, default
    return path, float(every)


def verdict(samples: list[dict], returns: list[dict], counts: tuple[int | None, int | None], cycles: int, floor: int) -> list[str]:
    """Why the run failed; empty on a pass. `counts`: the device's return count before and after."""
    start_count, end_count = counts
    problems = []
    ups = [s["uptime"] for s in samples if s.get("uptime") is not None]
    if any(b < a for a, b in zip(ups, ups[1:], strict=False)):
        problems.append("the device restarted")
    spent = [s["reserveSpent"] for s in samples if s.get("reserveSpent") is not None]
    if spent and spent[-1] != spent[0]:
        problems.append(f"the emergency reserve was spent {spent[-1] - spent[0]} times")
    if start_count is None or end_count is None:
        problems.append("the device's return record couldn't be read")
    elif end_count - start_count < cycles:
        problems.append(f"the device recorded {end_count - start_count} returns for {cycles} cuts")
    low = [s["largest"] for s in samples if s.get("largest") is not None]
    if low and min(low) < floor:
        problems.append(f"a status sample's largest block was {min(low)} B (floor {floor})")
    for r in returns[: max(cycles, 0)]:
        if r["largest"] is not None and r["largest"] < floor:
            problems.append(f"after the return at uptime {r['uptime']} s the largest block fell to {r['largest']} B")
    return problems


class Soak:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.base = f"http://{args.host}"
        self.samples: list[dict] = []
        self.failed = 0
        self.busy = 0
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.last_diag = 0.0

    def get(self, path: str, timeout: float = 5) -> tuple[int, bytes]:
        req = urllib.request.Request(self.base + path)
        if self.args.password:
            token = base64.b64encode(f"{self.args.user}:{self.args.password}".encode()).decode()
            req.add_header("Authorization", "Basic " + token)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as reply:
                return reply.status, reply.read()
        except urllib.error.HTTPError as err:
            return err.code, b""
        except (OSError, urllib.error.URLError):
            return 0, b""

    def note(self, code: int, path: str, body: bytes) -> None:
        with self.lock:
            if code == 0:
                self.failed += 1
            elif code == 503:
                self.busy += 1
        if code != 200 or not path.startswith("/api/status"):
            return
        try:
            doc = json.loads(body)
        except ValueError:
            return
        heap = doc.get("heap8Bit") or {}
        with self.lock:
            self.samples.append(
                {
                    "t": time.time(),
                    "uptime": doc.get("uptime"),
                    "largest": heap.get("largest"),
                    "free": heap.get("free"),
                    "reserveSpent": heap.get("reserveSpent"),
                }
            )

    def poller(self, path: str, every: float) -> None:
        while not self.stop.is_set():
            started = time.time()
            code, body = self.get(path)
            self.note(code, path, body)
            self.stop.wait(max(0.0, every - (time.time() - started)))

    def diag(self) -> tuple[int | None, list[dict]]:
        wait = DIAG_MIN_GAP_S - (time.time() - self.last_diag)
        if wait > 0:
            time.sleep(wait)
        for _ in range(5):
            self.last_diag = time.time()
            code, body = self.get("/api/diag/heap", timeout=10)
            if code == 200:
                return parse_returns(body.decode(errors="replace"))
            time.sleep(DIAG_MIN_GAP_S)
        return None, []

    def run_command(self, command: str, what: str) -> None:
        print(f"[{time.strftime('%H:%M:%S')}] {what}: {command}", flush=True)
        if command:
            subprocess.run(command, shell=True, check=False)  # noqa: S602 - the caller's own command

    def run(self) -> int:
        args = self.args
        code, _ = self.get("/api/status")
        if code == 0:
            print(f"{args.host} not reachable", file=sys.stderr)
            return 2
        start_count, _ = self.diag()
        threads = [threading.Thread(target=self.poller, args=split_poll(p, 1.0), daemon=True) for p in args.also]
        for t in threads:
            t.start()
        for cycle in range(1, args.cycles + 1):
            self.run_command(args.cut, f"cycle {cycle}/{args.cycles}: cut for {args.down:g} s")
            time.sleep(args.down)
            self.run_command(args.restore, f"cycle {cycle}/{args.cycles}: restored")
            time.sleep(max(args.up, FOLLOW_S + 5) if cycle == args.cycles else args.up)
        end_count, returns = self.diag()
        self.stop.set()
        for t in threads:
            t.join(timeout=10)
        problems = verdict(self.samples, returns, (start_count, end_count), args.cycles, args.floor)
        for r in returns[: args.cycles]:
            print(
                f"  return at uptime {r['uptime']} s ({r['kind']}): lowest largest {r['largest']} B, free {r['free']} B, "
                f"{r['refused']} busy at connect, at most {r['live']} open, MQTT after {r['mqtt']} s, {r['discovery']}"
            )
        lows = [s["largest"] for s in self.samples if s.get("largest") is not None]
        print(
            f"{len(self.samples)} status samples, lowest largest {min(lows) if lows else '-'} B; "
            f"{self.failed} requests failed, {self.busy} answered busy"
        )
        for p in problems:
            print("FAIL: " + p)
        print("PASS" if not problems else "FAIL")
        return 0 if not problems else 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", required=True, help="device address, e.g. 192.168.1.245")
    parser.add_argument("--cycles", type=int, default=5)
    parser.add_argument("--down", type=float, default=90, help="seconds the network is cut each cycle")
    parser.add_argument("--up", type=float, default=120, help="seconds the network stays up between cuts")
    parser.add_argument("--cut", default="", help="shell command that takes the network away")
    parser.add_argument("--restore", default="", help="shell command that brings it back")
    parser.add_argument("--also", nargs="*", default=["/api/status@1"], help='paths polled, each "path" or "path@seconds"')
    parser.add_argument("--floor", type=int, default=16384, help="lowest acceptable largest free block (bytes)")
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="", help="the device's password, when protection is on")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    return Soak(parse_args(argv)).run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
