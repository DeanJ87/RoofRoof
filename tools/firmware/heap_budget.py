#!/usr/bin/env python3
"""Heap budget check (spec 038): what the firmware takes from the heap for good.

The byte-addressable heap is what the ESP32's data RAM has left after the
firmware's static data (.dram0.data, .dram0.bss in the .elf), and each task's
stack and each buffer kept for the device's lifetime comes out of it. Spec 038
cut these to raise the largest free block at rest; this check keeps them cut:
it reads the sizes from the sources (platformio.ini, the headers) and, when the
build is there, the static data from the .elf, and fails when their sum grows
past tools/firmware/heap-budget.json. The device-side floor (largest block at
rest) is the bench check in tools/soak/ws_soak.py.

    python3 tools/firmware/heap_budget.py                      # check (CI, after the build)
    python3 tools/firmware/heap_budget.py --update-baseline --reason "why it grew"

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUDGET_PATH = ROOT / "tools/firmware/heap-budget.json"
ELF_PATH = ROOT / ".pio/build/esp32dev/firmware.elf"
STATIC_SECTIONS = (".dram0.data", ".dram0.bss")
# Room for ordinary growth of static data (a new setting, a counter) without a record.
SLACK_BYTES = 2048

# name -> (file, regex with one group: the size expression in bytes)
CONSUMERS = {
    "loop task stack": ("src/main.cpp", r"SET_LOOP_TASK_STACK_SIZE\(([^)]*)\)"),
    "async_tcp task stack": ("platformio.ini", r"-DCONFIG_ASYNC_TCP_STACK_SIZE=(\d+)"),
    "esp_timer task stack": ("platformio.ini", r"CONFIG_ESP_TIMER_TASK_STACK_SIZE=(\d+)"),
    "roof task stack": ("include/RoofControl.h", r"TASK_STACK_BYTES = (\d+)"),
    "safetyPoll task stack": ("include/SafetyPoller.h", r"TASK_STACK_BYTES = (\d+)"),
    "frame pool slot": ("lib/HeapSafe/include/FramePool.h", r"CAPACITY = (\d+)"),
    "frame pool slots": ("lib/HeapSafe/include/FramePool.h", r"SLOTS = (\d+)"),
    "emergency reserve": ("lib/HeapSafe/include/HeapReserve.h", r"BYTES = (\d+)"),
    "MQTT buffer": ("lib/RoofIoLogic/include/RoofMqtt.h", r"CLIENT_BUFFER_BYTES = (\d+)"),
}


def size_of(expr: str) -> int:
    """'10 * 1024' or '8192'."""
    if not re.fullmatch(r"[\d\s*]+", expr):
        raise ValueError(f"not a size: {expr!r}")
    total = 1
    for factor in expr.split("*"):
        total *= int(factor)
    return total


def read_consumers(root: Path = ROOT) -> dict[str, int]:
    found: dict[str, int] = {}
    for name, (path, pattern) in CONSUMERS.items():
        match = re.search(pattern, (root / path).read_text())
        if match is None:
            raise ValueError(f"{path}: no match for {name} ({pattern})")
        found[name] = size_of(match.group(1))
    slots = found.pop("frame pool slots")
    found["frame pool"] = found.pop("frame pool slot") * slots
    return found


def section_sizes(elf: bytes, names: tuple[str, ...]) -> dict[str, int]:
    """Sizes of the named sections of a 32-bit little-endian ELF."""
    if elf[:4] != b"\x7fELF" or elf[4] != 1:
        raise ValueError("not a 32-bit ELF")
    (shoff,) = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    headers = [struct.unpack_from("<IIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    strtab = headers[shstrndx][4]
    sizes: dict[str, int] = {}
    for name_off, _type, _flags, _addr, _offset, size in headers:
        end = elf.index(b"\0", strtab + name_off)
        name = elf[strtab + name_off : end].decode()
        if name in names:
            sizes[name] = size
    return sizes


def evaluate(consumers: dict[str, int], static: int | None, budget: dict) -> tuple[int, list[str], bool]:
    total = sum(consumers.values()) + (static or 0)
    lines = [f"  {name:24} {size:>8,} B" for name, size in sorted(consumers.items(), key=lambda kv: -kv[1])]
    if static is not None:
        lines.append(f"  {'static data (.elf)':24} {static:>8,} B")
    limit = budget["limitBytes"] if static is not None else budget["limitBytes"] - budget["staticBytes"]
    lines.append(f"  {'total':24} {total:>8,} B of {limit:,} B budget")
    return total, lines, total <= limit


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--elf", type=Path, default=ELF_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--reason", default="")
    args = parser.parse_args(argv)

    consumers = read_consumers()
    static = None
    if args.elf.exists():
        static = sum(section_sizes(args.elf.read_bytes(), STATIC_SECTIONS).values())
    budget = json.loads(BUDGET_PATH.read_text())
    if args.update_baseline:
        if static is None or not args.reason:
            sys.exit("heap_budget.py: --update-baseline needs a build (.elf) and --reason")
        budget.update(limitBytes=sum(consumers.values()) + static + SLACK_BYTES, staticBytes=static, reason=args.reason)
        BUDGET_PATH.write_text(json.dumps(budget, indent=2) + "\n")
    total, lines, ok = evaluate(consumers, static, budget)
    print("Kept from the heap for the device's lifetime (spec 038):")
    print("\n".join(lines))
    if not ok:
        print(
            "HEAP-01: the firmware keeps more of the heap than tools/firmware/heap-budget.json allows. "
            "Find another saving, or record why with --update-baseline --reason.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
