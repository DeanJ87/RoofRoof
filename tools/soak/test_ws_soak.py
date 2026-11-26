"""Unit tests for the live-page soak tool's helpers (no device needed)."""

import io
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ws_soak as soak  # noqa: E402 - imported once its folder is on sys.path


class FakeSocket:
    def __init__(self, data: bytes):
        self.stream = io.BytesIO(data)

    def recv(self, n: int) -> bytes:
        return self.stream.read(n)


class WsSoakTest(unittest.TestCase):
    def test_verdict_floors_and_restarts(self):
        rows = [
            {"boot": 1, "uptime": 10, "largest": 45000, "free": 60000},
            {"boot": 1, "uptime": 20, "largest": 41000, "free": 16000},
        ]
        self.assertEqual(soak.verdict(rows, 40960), (False, True))
        self.assertEqual(soak.verdict(rows, 40960, free_floor=15360), (False, True))
        self.assertEqual(soak.verdict(rows, 40960, free_floor=16384), (False, False))
        self.assertEqual(soak.verdict(rows, 41001), (False, False))
        restarted = rows + [{"boot": 1, "uptime": 5, "largest": 50000, "free": 60000}]
        self.assertEqual(soak.verdict(restarted, 0), (True, False))

    def test_drift_compares_the_best_of_the_start_and_the_end(self):
        rows = [
            {"boot": 1, "uptime": i, "largest": v, "free": 60000} for i, v in enumerate([61000, 30000, 58000, 40000, 50000] + [20000] * 5)
        ]
        self.assertEqual(soak.drift(rows), 61000 - 20000)
        self.assertEqual(soak.verdict(rows, 0, max_drift=50000), (False, True))
        self.assertEqual(soak.verdict(rows, 0, max_drift=12288), (False, False))
        steady = [{"boot": 1, "uptime": i, "largest": 40000 + (i % 2) * 20000, "free": 60000} for i in range(12)]
        self.assertEqual(soak.drift(steady), 0)
        self.assertEqual(soak.drift([]), 0)

    def test_page_assets_from_index_html(self):
        html = (
            '<link rel="icon" href="/favicon.svg"><script type="module" crossorigin src="/assets/App-DT.js"></script>'
            '<link rel="stylesheet" href="/assets/CRK.css"><link rel="modulepreload" href="/assets/App-DT.js">'
            '<a href="#top">x</a><a href="/">home</a>'
        )
        self.assertEqual(soak.page_assets(html), ["/favicon.svg", "/assets/App-DT.js", "/assets/CRK.css"])

    def test_a_page_load_fetches_the_files_and_keeps_the_hashed_ones(self):
        index = b'<script src="/assets/a.js"></script><link href="/assets/b.css"><link href="/favicon.svg">'
        fetched: list[str] = []

        def get(path, timeout=8):
            fetched.append(path)
            return (200, index) if path == "/" else (503 if path == "/favicon.svg" else 200, b"")

        cached: set[str] = set()
        self.assertEqual(soak.fetch_page(get, cached), (False, 3))  # the icon was busy
        self.assertEqual(cached, {"/assets/a.js", "/assets/b.css"})
        fetched.clear()
        soak.fetch_page(get, cached)
        self.assertEqual(sorted(fetched), ["/", "/favicon.svg"])  # the hashed files came from the cache
        self.assertEqual(soak.fetch_page(lambda path, timeout=8: (500, b""))[0], False)

    def test_request_is_an_upgrade(self):
        request = soak.websocket_request("10.0.0.5", "/ws/status").decode()
        self.assertTrue(request.startswith("GET /ws/status HTTP/1.1\r\n"))
        self.assertIn("Upgrade: websocket\r\n", request)

    def test_reads_a_frame_with_a_16_bit_length(self):
        payload = b"x" * 4400
        frame = bytes([0x81, 126]) + struct.pack(">H", len(payload)) + payload
        self.assertEqual(soak.read_frame(FakeSocket(frame)), (1, payload))

    def test_close_code_of_a_refused_page(self):
        self.assertEqual(soak.close_code(struct.pack(">H", 4429) + b"too many open pages"), soak.TOO_MANY_PAGES)
        self.assertEqual(soak.close_code(b""), 1005)

    def test_client_frames_are_masked(self):
        frame = soak.client_frame(0xA, b"hi")
        self.assertEqual(frame[0], 0x8A)
        self.assertEqual(frame[1], 0x80 | 2)
        mask = frame[2:6]
        self.assertEqual(bytes(b ^ mask[i % 4] for i, b in enumerate(frame[6:])), b"hi")

    def test_poll_spec_and_backoff(self):
        self.assertEqual(soak.split_poll("/api/status@0.5", 2), ("/api/status", 0.5))
        self.assertEqual(soak.split_poll("/api/diag", 2), ("/api/diag", 2))
        self.assertEqual(soak.reconnect_delay(0), 1)
        self.assertEqual(soak.reconnect_delay(10), 30)

    def test_a_closed_stream_raises(self):
        with self.assertRaises(ConnectionError):
            soak.read_exact(FakeSocket(b"\x81"), 2)


if __name__ == "__main__":
    unittest.main()
