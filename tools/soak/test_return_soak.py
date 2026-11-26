"""Unit tests for the network-return soak's helpers (no device needed)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import return_soak as soak  # noqa: E402 - imported once its folder is on sys.path

CENSUS = """Heap (8-bit): free 63680 B in 26 blocks, largest 38900 B, lowest since start 1416 B; in use 184252 B in 664 blocks; reserve held, spent 0 times
Trend (lowest largest block / lowest free, per minute, newest first; 15 min 1012 B, 60 min 1012 B):
  minute    41  largest  24564 B  free  49288 B
  minute    40  largest   3316 B  free  10540 B  <- network return
Network returns (newest first; 2 since start; the heap's lows over the 60 s after each):
  minute    40, uptime 2439 s: link back after 121 s; lowest largest 17396 B, free 30540 B; 12 busy at connect, at most 3 open; MQTT after 10.4 s; discovery kept by the broker
  minute    38, uptime 2300 s: requests back after 45 s without any (link kept); no heap reading yet; 0 busy at connect, at most 0 open; MQTT not connected since; discovery not sent yet (still following)
  minute     0, uptime 5 s: first on a network; lowest largest 30000 B, free 50000 B; 0 busy at connect, at most 1 open; MQTT after 3.0 s; discovery published again
"""


class ReturnSoakTest(unittest.TestCase):
    def test_the_census_return_lines_are_read(self):
        count, returns = soak.parse_returns(CENSUS)
        self.assertEqual(count, 2)
        self.assertEqual(len(returns), 3)
        self.assertEqual(returns[0]["uptime"], 2439)
        self.assertEqual(returns[0]["kind"], "link back after 121 s")
        self.assertEqual(returns[0]["largest"], 17396)
        self.assertEqual(returns[0]["refused"], 12)
        self.assertEqual(returns[0]["live"], 3)
        self.assertEqual(returns[0]["mqtt"], 10.4)
        self.assertIsNone(returns[1]["largest"])
        self.assertIsNone(returns[1]["mqtt"])
        self.assertEqual(returns[1]["kind"], "requests back after 45 s without any (link kept)")

    def test_a_clean_run_passes(self):
        samples = [{"uptime": 10, "largest": 30000, "reserveSpent": 0}, {"uptime": 20, "largest": 20000, "reserveSpent": 0}]
        returns = [{"uptime": 15, "largest": 17000}]
        self.assertEqual(soak.verdict(samples, returns, (3, 4), 1, 16384), [])

    def test_each_failure_is_named(self):
        samples = [
            {"uptime": 10, "largest": 30000, "reserveSpent": 0},
            {"uptime": 5, "largest": 15000, "reserveSpent": 1},
        ]
        returns = [{"uptime": 15, "largest": 9000}, {"uptime": 8, "largest": 1000}]
        problems = soak.verdict(samples, returns, (3, 4), 2, 16384)
        self.assertEqual(len(problems), 6, problems)
        self.assertIn("restarted", problems[0])
        self.assertIn("reserve was spent 1 times", problems[1])
        self.assertIn("recorded 1 returns for 2 cuts", problems[2])
        self.assertIn("15000 B", problems[3])
        self.assertIn("9000 B", problems[4])
        self.assertIn("1000 B", problems[5])
        self.assertIn("couldn't be read", soak.verdict([], [], (None, 4), 1, 0)[0])

    def test_polls_are_split_from_their_interval(self):
        self.assertEqual(soak.split_poll("/api/status@0.5", 1.0), ("/api/status", 0.5))
        self.assertEqual(soak.split_poll("/api/status", 1.0), ("/api/status", 1.0))


if __name__ == "__main__":
    unittest.main()
