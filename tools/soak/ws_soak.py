#!/usr/bin/env python3
"""Live-page soak (spec 037): N open pages on /ws/status plus HTTP polling, heap tracked.

Each simulated page keeps one status WebSocket and behaves like the web UI:
it reconnects when the socket closes (1 s back-off doubling to 30 s) or when
it has sent nothing for --quiet seconds. A close with code 4429 (too many open
pages) is counted and retried slowly (--refused-retry). Meanwhile GET
/api/status is read every --sample seconds for the heap figures, and any
--also paths are polled the way a page polls them. Read-only: nothing is saved
or moved.

    python3 tools/soak/ws_soak.py --host 192.168.1.245 --clients 3 --minutes 10
    python3 tools/soak/ws_soak.py --host 192.168.1.245 --clients 4 --quiet 5   # the old page
    # A test recorder, an imaging app and the diagnostics page beside the pages:
    python3 tools/soak/ws_soak.py --host 192.168.1.245 --clients 4 \
        --also /api/status@0.5 /api/diag@10 "/api/v1/dome/0/shutterstatus?ClientID=1&ClientTransactionID=1@1"

Pass: the device never restarted and its largest free block stayed at or above
--floor bytes. Exit status 0 on a pass, 1 on a fail, 2 when it couldn't be
reached. Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field

TOO_MANY_PAGES = 4429


def websocket_request(host: str, path: str) -> bytes:
    key = base64.b64encode(os.urandom(16)).decode()
    return (
        f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    ).encode()


def client_frame(opcode: int, payload: bytes = b"") -> bytes:
    """A masked client frame (control frames only here: payload < 126 bytes)."""
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes([0x80 | opcode, 0x80 | len(payload)]) + mask + masked


def read_exact(sock: socket.socket, n: int) -> bytes:
    out = b""
    while len(out) < n:
        piece = sock.recv(n - len(out))
        if not piece:
            raise ConnectionError("closed")
        out += piece
    return out


def read_frame(sock: socket.socket) -> tuple[int, bytes]:
    """(opcode, payload) of the next server frame (servers don't mask)."""
    first, second = read_exact(sock, 2)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack(">H", read_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", read_exact(sock, 8))[0]
    return first & 0x0F, read_exact(sock, length)


def close_code(payload: bytes) -> int:
    return struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else 1005


def split_poll(spec: str, default: float) -> tuple[str, float]:
    """ "/api/status@0.5" -> ("/api/status", 0.5); without "@", the default interval."""
    path, _, every = spec.partition("@")
    return path, float(every) if every else default


def reconnect_delay(retry: int) -> float:
    return min(30.0, 2.0**retry)


@dataclass
class Page:
    index: int
    frames: int = 0
    heartbeats: int = 0
    connects: int = 0
    quiet_drops: int = 0
    refused: int = 0
    closes: dict[int, int] = field(default_factory=dict)
    max_gap: float = 0.0


class Soak:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.stop = threading.Event()
        self.pages = [Page(i) for i in range(args.clients)]
        self.samples: list[dict] = []
        self.http: dict[str, dict[str, int]] = {}
        self.lock = threading.Lock()

    def connect(self) -> socket.socket:
        sock = socket.create_connection((self.args.host, 80), timeout=5)
        sock.sendall(websocket_request(self.args.host, "/ws/status"))
        head = b""
        while b"\r\n\r\n" not in head:
            piece = sock.recv(1)
            if not piece:
                raise ConnectionError("closed during handshake")
            head += piece
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError(head.split(b"\r\n", 1)[0].decode(errors="replace"))
        return sock

    def session(self, page: Page) -> int | None:
        """One connection until it ends; the close code, or None for a quiet drop or error."""
        sock = self.connect()
        page.connects += 1
        sock.settimeout(self.args.quiet)
        last = time.monotonic()
        try:
            while not self.stop.is_set():
                opcode, payload = read_frame(sock)
                now = time.monotonic()
                if opcode == 0x8:
                    return close_code(payload)
                if opcode == 0x9:
                    sock.sendall(client_frame(0xA, payload))
                    continue
                if opcode in (0x1, 0x0):
                    page.max_gap = max(page.max_gap, now - last)
                    last = now
                    if b'"heartbeat"' in payload[:24]:
                        page.heartbeats += 1
                    else:
                        page.frames += 1
            return 1000
        except TimeoutError:
            page.quiet_drops += 1
            return None
        finally:
            sock.close()

    def run_page(self, page: Page) -> None:
        retry = 0
        while not self.stop.is_set():
            try:
                code = self.session(page)
            except OSError:
                code = 1006
            if code is not None:
                page.closes[code] = page.closes.get(code, 0) + 1
            if code == TOO_MANY_PAGES:
                page.refused += 1
                self.stop.wait(self.args.refused_retry)
                continue
            # The page reconnects at once after a quiet drop, else after its back-off.
            if code is None:
                retry = 0
                continue
            self.stop.wait(reconnect_delay(retry))
            retry = min(retry + 1, 5)

    def get(self, path: str, timeout: float = 8) -> tuple[int, bytes]:
        try:
            with urllib.request.urlopen(f"http://{self.args.host}{path}", timeout=timeout) as reply:
                return reply.status, reply.read()
        except urllib.error.HTTPError as error:
            return error.code, b""

    def poll(self, spec: str) -> None:
        path, every = split_poll(spec, self.args.http_every)
        while not self.stop.wait(every):
            try:
                code, _ = self.get(path)
                key = "ok" if code == 200 else "busy" if code == 503 else "failed"
            except (OSError, urllib.error.URLError) as exc:
                key = "timeout" if "timed out" in str(exc).lower() else "failed"
            with self.lock:
                counts = self.http.setdefault(path.split("?")[0], {"ok": 0, "busy": 0, "timeout": 0, "failed": 0})
                counts[key] += 1

    def sample(self) -> dict | None:
        for _ in range(3):  # a "busy" (503) reply is retried, not taken for a dead device
            row = self.sample_once()
            if row is not None:
                return row
            time.sleep(1)
        return None

    def sample_once(self) -> dict | None:
        try:
            code, body = self.get("/api/status")
            doc = json.loads(body) if code == 200 else None
        except (OSError, urllib.error.URLError, ValueError):
            return None
        if not doc:
            return None
        conn = doc.get("connections", {})
        return {
            "t": round(time.monotonic() - self.started),
            "uptime": doc.get("uptime"),
            "boot": doc.get("bootCount"),
            # The byte-addressable heap (spec 037); maxAllocHeap also counts IRAM.
            "free": doc.get("heap8Bit", {}).get("free", doc.get("freeHeap")),
            "largest": doc.get("heap8Bit", {}).get("largest", doc.get("maxAllocHeap")),
            "minFree": doc.get("heap8Bit", {}).get("minFree", doc.get("minFreeHeap")),
            "live": conn.get("liveUpdates", {}).get("status"),
            "liveCounters": {k: v for k, v in conn.get("liveUpdates", {}).items() if k not in ("status", "limitPerEndpoint")},
            "short": conn.get("shortOfMemory"),
            "roof": doc.get("roof", {}).get("state"),
        }

    def run(self) -> int:
        self.started = time.monotonic()
        first = self.sample()
        if first is None:
            print("The device didn't answer GET /api/status.", file=sys.stderr)
            return 2
        self.samples.append(first)
        workers = [threading.Thread(target=self.run_page, args=(p,), daemon=True) for p in self.pages]
        workers += [threading.Thread(target=self.poll, args=(path,), daemon=True) for path in self.args.also]
        for worker in workers:
            worker.start()
        end = self.started + self.args.minutes * 60
        try:
            while time.monotonic() < end:
                time.sleep(self.args.sample)
                row = self.sample()
                if row:
                    self.samples.append(row)
                    print(json.dumps(row), flush=True)
                else:
                    print(json.dumps({"t": round(time.monotonic() - self.started), "error": "no reply"}), flush=True)
        except KeyboardInterrupt:
            pass
        self.stop.set()
        return self.report(first)

    def report(self, first: dict) -> int:
        last = self.samples[-1]
        restarted = last["boot"] != first["boot"] or any(
            b["uptime"] < a["uptime"] for a, b in zip(self.samples, self.samples[1:], strict=False)
        )
        lowest = min(s["largest"] for s in self.samples)
        summary = {
            "minutes": self.args.minutes,
            "clients": self.args.clients,
            "restarted": restarted,
            "largestBlock": {"first": first["largest"], "lowest": lowest, "last": last["largest"]},
            "freeHeap": {"first": first["free"], "lowest": min(s["free"] for s in self.samples), "last": last["free"]},
            "http": self.http,
            "pages": [
                {
                    "connects": p.connects,
                    "frames": p.frames,
                    "heartbeats": p.heartbeats,
                    "quietDrops": p.quiet_drops,
                    "refused": p.refused,
                    "closes": p.closes,
                    "maxGapS": round(p.max_gap, 1),
                }
                for p in self.pages
            ],
            "device": {"first": first, "last": last},
        }
        print(json.dumps(summary, indent=2))
        passed = not restarted and lowest >= self.args.floor
        print("PASS" if passed else "FAIL")
        return 0 if passed else 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", required=True, help="device address, e.g. 192.168.1.245")
    parser.add_argument("--clients", type=int, default=3, help="open pages, one status socket each")
    parser.add_argument("--minutes", type=float, default=10)
    parser.add_argument("--quiet", type=float, default=10, help="seconds without a frame before a page reconnects")
    parser.add_argument("--refused-retry", type=float, default=60, help="seconds a refused page waits")
    parser.add_argument("--sample", type=float, default=10, help="seconds between heap samples")
    parser.add_argument("--also", nargs="*", default=["/api/status"], help='paths polled, each "path" or "path@seconds"')
    parser.add_argument("--http-every", type=float, default=2, help="seconds between polls of each --also path")
    parser.add_argument("--floor", type=int, default=20480, help="lowest acceptable largest free block (bytes)")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    return Soak(parse_args(argv)).run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
