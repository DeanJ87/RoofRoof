"""Tests for tools/decode_crash.py (spec 029 FR-021), with a stand-in addr2line."""

from __future__ import annotations

import gzip
import hashlib
import io
import os
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import decode_crash  # noqa: E402 - imported after its folder is on the path

FAKE_ADDR2LINE = """#!/usr/bin/env python3
import sys
args = sys.argv[1:]
elf = args[args.index("-e") + 1]
open(elf, "rb").read()
for address in args[args.index("-e") + 2:]:
    print(f"{address}: fn_{address[-4:]} at /src/file.cpp:{int(address, 16) % 1000}")
"""

ELF = b"\x7fELF fake firmware"
BUILD = hashlib.sha256(ELF).hexdigest()[:16]


def record(seq=7, kind="panic", build=BUILD, backtrace="0x400d5f2c 0x400d61a0", reason="LoadProhibited"):
    lines = [
        f"Record {seq}: {kind}",
        "Time: 2026-11-25 02:14:07 UTC, 5231 s after start",
        f"Reset reason: {kind} (4)",
        f"Firmware: 0.1.0+dev, build {build}",
        f"Reason: {reason}",
    ]
    if backtrace:
        lines += ["Task: async_tcp, core 1, stack left 1904 B", f"Backtrace: {backtrace}"]
    return "\n".join(lines) + "\n"


def document(*records):
    head = "This start: panic (4), boot 3\nRunning: 0.1.0+dev, build " + BUILD + "\nRecords: 2 (the last 3 are kept)\n"
    return head + "".join("\n" + r for r in records)


class DecodeCrashTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.dir = Path(self.folder.name)
        self.addr2line = self.dir / "addr2line"
        self.addr2line.write_text(FAKE_ADDR2LINE)
        self.addr2line.chmod(self.addr2line.stat().st_mode | stat.S_IEXEC)
        self.elf = self.dir / "firmware.elf"
        self.elf.write_bytes(ELF)

    def tearDown(self):
        self.folder.cleanup()

    def run_tool(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = decode_crash.main([*argv, "--addr2line", str(self.addr2line)])
        return code, out.getvalue(), err.getvalue()

    def write(self, text):
        path = self.dir / "crash.txt"
        path.write_text(text)
        return str(path)

    def test_addresses_from_text_takes_pcs(self):
        self.assertEqual(
            decode_crash.addresses_from_text("Backtrace: 0x400D5F2C:0x3ffb1230 0x400d61a0:0x3ffb1250, junk 0x12"),
            ["0x400d5f2c", "0x400d61a0"],
        )

    def test_records_from_a_document_or_one_record(self):
        records = decode_crash.records_from_text(document(record(), record(seq=6, kind="brownout", backtrace="")))
        self.assertEqual([r["seq"] for r in records], [7, 6])
        self.assertEqual(records[0]["build"], BUILD)
        self.assertEqual(records[0]["version"], "0.1.0+dev")
        self.assertEqual(records[1]["decode"], "")
        self.assertEqual(len(decode_crash.records_from_text(record())), 1)
        with self.assertRaises(decode_crash.DecodeError):
            decode_crash.records_from_text("nothing here")

    def test_decodes_a_record(self):
        code, out, _ = self.run_tool(self.write(document(record())), "--elf", str(self.elf))
        self.assertEqual(code, 0)
        self.assertIn("Record 7: panic | LoadProhibited | task async_tcp, core 1, stack left 1904 B | firmware 0.1.0+dev", out)
        self.assertIn("0x400d5f2c: fn_5f2c at /src/file.cpp:", out)
        self.assertIn("0x400d61a0: fn_61a0", out)

    def test_gzipped_elf(self):
        gz = self.dir / "firmware.elf.gz"
        gz.write_bytes(gzip.compress(ELF))
        code, out, _ = self.run_tool(self.write(record()), "--elf", str(gz))
        self.assertEqual(code, 0)
        self.assertIn("fn_5f2c", out)

    def test_refuses_another_build_unless_forced(self):
        path = self.write(record(build="0000000000000000"))
        code, _, err = self.run_tool(path, "--elf", str(self.elf))
        self.assertEqual(code, 1)
        self.assertIn(f"the .elf is build {BUILD}, the record is build 0000000000000000", err)
        code, out, _ = self.run_tool(path, "--elf", str(self.elf), "--force")
        self.assertEqual(code, 0)
        self.assertIn("WARNING", out)
        self.assertIn("fn_5f2c", out)

    def test_record_without_capture(self):
        code, out, _ = self.run_tool(self.write(record(kind="brownout", backtrace="", reason="not captured")), "--elf", str(self.elf))
        self.assertEqual(code, 0)
        self.assertIn("no backtrace was captured", out)

    def test_corrupt_and_continues_noted(self):
        code, out, _ = self.run_tool(self.write(record(backtrace="0x400d5f2c 0x400d61a0 [corrupt] [continues]")), "--elf", str(self.elf))
        self.assertEqual(code, 0)
        self.assertIn("corrupt frame", out)
        self.assertIn("more frames", out)

    def test_plain_addresses(self):
        code, out, _ = self.run_tool("--addrs", "0x400d5f2c:0x3ffb1230 0x400d61a0:0x3ffb1250", "--elf", str(self.elf))
        self.assertEqual(code, 0)
        self.assertIn(f"Build {BUILD}", out)
        self.assertIn("fn_61a0", out)

    def test_not_a_record(self):
        path = self.dir / "bad.txt"
        path.write_text("not a record")
        code, _, err = self.run_tool(str(path), "--elf", str(self.elf))
        self.assertEqual(code, 1)
        self.assertIn("no crash record", err)

    def test_missing_addr2line_is_explained(self):
        old = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        try:
            original = decode_crash.PIO_TOOLCHAIN
            decode_crash.PIO_TOOLCHAIN = self.dir / "none"
            with self.assertRaises(decode_crash.DecodeError):
                decode_crash.find_addr2line(None)
        finally:
            decode_crash.PIO_TOOLCHAIN = original
            os.environ["PATH"] = old


if __name__ == "__main__":
    unittest.main()
