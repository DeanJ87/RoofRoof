#!/usr/bin/env python3
"""Turn a RoofRoof crash record's addresses into functions, files and lines (spec 029).

The record comes from GET /api/diag/crashes or the Copy button on the
Diagnostics page (the whole text document or one record; a file, or stdin
with "-"), or as plain addresses with --addrs ("0x400d5f2c 0x400d61a0", or the
serial monitor's "0x400d5f2c:0x3ffb1230 ..." pairs). The .elf is the
firmware.elf of the same build: the CI artifact "firmware-elf", the release's
roofroof-l2-firmware-<tag>.elf.gz, or a local build's .pio/elf/<build>.elf.gz.

A record names its build by the first 16 hex digits of the .elf's SHA-256;
an .elf that doesn't match is refused unless --force is given, since its
addresses would point at the wrong code.

    python3 tools/decode_crash.py crash.txt --elf .pio/elf/3f2a9c0e11d4b7a2.elf.gz
    python3 tools/decode_crash.py --addrs "0x400d5f2c:0x3ffb1230 0x400d61a0:0x3ffb1250" --elf firmware.elf
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ADDR2LINE = "xtensa-esp32-elf-addr2line"
PIO_TOOLCHAIN = Path.home() / ".platformio" / "packages" / "toolchain-xtensa-esp-elf" / "bin"
ADDRESS = re.compile(r"0x[0-9a-fA-F]{8}")


class DecodeError(Exception):
    """Why a record can't be decoded, in words for the person running the tool."""


def addresses_from_text(text: str) -> list[str]:
    """The program addresses in `text`; in "pc:sp" pairs only the pc."""
    out = []
    for token in text.replace(",", " ").split():
        match = ADDRESS.match(token)
        if match:
            out.append(match.group(0).lower())
    return out


def records_from_text(text: str) -> list[dict]:
    """The records in a crashes document or a copied record: each block from "Record <seq>: <kind>"."""
    records = []
    for block in re.split(r"\n\s*\n(?=Record \d+: )", "\n" + text.strip()):
        head = re.match(r"\s*Record (\d+): (\w+)", block)
        if not head:
            continue
        fields = dict(re.findall(r"^(\w[\w ]*): (.*)$", block, re.M))
        build = re.search(r"build ([0-9a-f]{16})", fields.get("Firmware", ""))
        backtrace = fields.get("Backtrace", "")
        records.append(
            {
                "seq": int(head.group(1)),
                "kind": head.group(2),
                "reason": fields.get("Reason"),
                "task": fields.get("Task"),
                "version": fields.get("Firmware", "").split(",")[0] or None,
                "build": build.group(1) if build else None,
                "decode": backtrace,
                "backtraceCorrupt": "[corrupt]" in backtrace,
                "backtraceContinues": "[continues]" in backtrace,
            }
        )
    if not records:
        raise DecodeError("no crash record in the text (expected GET /api/diag/crashes or a record copied from it)")
    return records


def elf_build_id(elf_bytes: bytes) -> str:
    """The build id a record names: the first 16 hex digits of the .elf's SHA-256."""
    return hashlib.sha256(elf_bytes).hexdigest()[:16]


def read_elf(path: Path) -> bytes:
    data = path.read_bytes()
    return gzip.decompress(data) if data[:2] == b"\x1f\x8b" else data


def find_addr2line(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which(ADDR2LINE)
    if found:
        return found
    candidate = PIO_TOOLCHAIN / ADDR2LINE
    if candidate.exists():
        return str(candidate)
    raise DecodeError(f"{ADDR2LINE} not found: install PlatformIO's ESP32 toolchain or pass --addr2line")


def symbolise(addr2line: str, elf_path: str, addresses: list[str]) -> list[str]:
    """One line per address: "0x400d5f2c: function at file:line" (inlined frames follow, indented)."""
    if not addresses:
        return []
    result = subprocess.run([addr2line, "-pfiaC", "-e", elf_path, *addresses], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise DecodeError(f"addr2line failed: {result.stderr.strip()}")
    return [line.rstrip() for line in result.stdout.splitlines() if line.strip()]


def header(record: dict) -> str:
    parts = [f"Record {record.get('seq', '?')}: {record.get('kind', '?')}"]
    if record.get("reason"):
        parts.append(str(record["reason"]))
    if record.get("task"):
        parts.append(f"task {record['task']}")
    if record.get("version"):
        parts.append(f"firmware {record['version']}")
    return " | ".join(parts)


def decode_record(record: dict, elf_bytes: bytes, elf_path: str, addr2line: str, force: bool) -> list[str]:
    lines = [header(record)]
    wanted = record.get("build")
    have = elf_build_id(elf_bytes)
    if wanted and wanted != have:
        message = f"the .elf is build {have}, the record is build {wanted}"
        if not force:
            raise DecodeError(message + " (use the .elf of that build, or --force)")
        lines.append(f"WARNING: {message}; the names may be wrong")
    addresses = addresses_from_text(str(record.get("decode") or ""))
    if not addresses:
        lines.append("  no backtrace was captured (brown-out, watchdog reset without a panic, or capture lost)")
        return lines
    if record.get("backtraceCorrupt"):
        lines.append("  (the backtrace hit a corrupt frame; the last frames may be missing)")
    lines.extend("  " + line for line in symbolise(addr2line, elf_path, addresses))
    if record.get("backtraceContinues"):
        lines.append("  ... (more frames than the record keeps)")
    return lines


def load_document(source: str) -> str:
    return sys.stdin.read() if source == "-" else Path(source).read_text(encoding="utf-8")


def run(args: argparse.Namespace) -> list[str]:
    elf_bytes = read_elf(Path(args.elf))
    addr2line = find_addr2line(args.addr2line)
    with tempfile.TemporaryDirectory() as folder:
        # addr2line needs a plain file; a gzipped .elf is unpacked here.
        elf_path = os.path.join(folder, "firmware.elf")
        Path(elf_path).write_bytes(elf_bytes)
        if args.addrs:
            return [f"Build {elf_build_id(elf_bytes)}"] + [
                "  " + line for line in symbolise(addr2line, elf_path, addresses_from_text(args.addrs))
            ]
        out: list[str] = []
        for record in records_from_text(load_document(args.record)):
            out.extend(decode_record(record, elf_bytes, elf_path, addr2line, args.force))
            out.append("")
        return out


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("record", nargs="?", help="crash record text file, or - for stdin")
    parser.add_argument("--elf", required=True, help="firmware.elf of the same build (plain or .gz)")
    parser.add_argument("--addrs", help="addresses instead of a record")
    parser.add_argument("--addr2line", help=f"path to {ADDR2LINE}")
    parser.add_argument("--force", action="store_true", help="decode with an .elf of another build")
    args = parser.parse_args(argv)
    if not args.addrs and not args.record:
        parser.error("give a crash record (file or -) or --addrs")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        print("\n".join(run(args)).rstrip())
    except (DecodeError, OSError) as error:
        print(f"decode_crash: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
