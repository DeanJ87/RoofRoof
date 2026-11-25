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
