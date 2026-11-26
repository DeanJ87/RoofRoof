#!/usr/bin/env python3
"""Stack depth check (fix 041): the deepest call path of a settings save fits its task.

A settings save on the async_tcp task's 8 KB stack (spec 038) overflowed it:
the settings parse alone has a 3 KB frame, and the analog checks' snprintf
went into the C library's float formatting and malloc below that. Saves now
run on their own task (include/SaveTask.h), and the restore preview on the
backup task; this check reads the build and keeps them within their stacks.

How: the .elf is disassembled (the Xtensa property sections stripped, so the
LTO-built flash code decodes); each function's frame is its `entry a1, N`, and
its callees are its direct calls and `callx` through a literal the
disassembler names. The deepest path from each root is summed. Calls through
a pointer (std::function, virtual) aren't followed, so a check names the
stages a save goes through, outermost first: a stage's depth counts on top of
the frames of the stages above it. Paths that end the device (abort, assert,
panic) and logging inside the heap's failure path are left out. Recursion is
counted once.

The second check keeps the save off async_tcp: the web server's save callback
must start the save task, and no web handler may reach Config::applyJson, Config::save or
Config::validate by a direct call (only through updateConfigCallback).

    python3 tools/firmware/stack_depth.py              # after `pio run -e esp32dev`
    python3 tools/firmware/stack_depth.py --path       # also print each worst path

Standard library only; needs the PlatformIO Xtensa toolchain (objdump, objcopy).
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ELF_PATH = ROOT / ".pio/build/esp32dev/firmware.elf"
TOOLCHAIN = Path.home() / ".platformio/packages/toolchain-xtensa-esp-elf/bin"

# Room above the deepest path: an interrupt's saved context on the task's
# stack (about 256 B on the ESP32) and what the stage model can't see.
MARGIN_BYTES = 1024

# Not followed: the device stops there, or only on the heap's failure path.
CUT = {
    "abort",
    "__assert_func",
    "__assert",
    "esp_system_abort",
    "panic_abort",
    "esp_log",
    "esp_log_write",
    "esp_log_writev",
    "_esp_error_check_failed",
    "__wrap_esp_panic_handler",
}

HEADER = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
ENTRY = re.compile(r"\tentry\ta1, (0x[0-9a-f]+|\d+)")
CALL = re.compile(r"\tcall(?:0|4|8|12)\t[0-9a-f]+ <([^>+]+)>")
LITERAL = re.compile(r"\tl32r\t(a\d+), .*\([0-9a-f]+ <([^>+]+)>\)")
CALLX = re.compile(r"\tcallx(?:0|4|8|12)\t(a\d+)")


@dataclass
class Graph:
    frame: dict[str, int] = field(default_factory=dict)
    calls: dict[str, set[str]] = field(default_factory=dict)
    memo: dict[str, tuple[int, list[str]]] = field(default_factory=dict)

    def names(self, pattern: str) -> list[str]:
        rx = re.compile(pattern)
        return [name for name in self.frame if rx.search(name)]

    def depth(self, name: str, active: set[str] | None = None) -> tuple[int, list[str]]:
        """The deepest stack from entering `name`, and that path."""
        if name in self.memo:
            return self.memo[name]
        active = set() if active is None else active
        if name in active:
            return 0, []
        active.add(name)
        best: tuple[int, list[str]] = (0, [])
        for callee in sorted(self.calls.get(name, ())):
            if callee in CUT:
                continue
            found = self.depth(callee, active)
            if found[0] > best[0]:
                best = found
        active.discard(name)
        result = (self.frame.get(name, 0) + best[0], [name] + best[1])
        self.memo[name] = result
        return result

    def reaches(self, name: str, targets: set[str], seen: set[str]) -> list[str] | None:
        if name in targets:
            return [name]
        if name in seen:
            return None
        seen.add(name)
        for callee in sorted(self.calls.get(name, ())):
            path = self.reaches(callee, targets, seen)
            if path:
                return [name] + path
        return None


def parse(lines) -> Graph:
    graph = Graph()
    name = None
    registers: dict[str, str] = {}
    for line in lines:
        m = HEADER.match(line)
        if m:
            name = m.group(2)
            graph.frame.setdefault(name, 0)
            graph.calls.setdefault(name, set())
            registers = {}
            continue
        if name is None:
            continue
        m = ENTRY.search(line)
        if m and graph.frame[name] == 0:
            graph.frame[name] = int(m.group(1), 0)
            continue
        m = CALL.search(line)
        if m:
            graph.calls[name].add(m.group(1))
            continue
        m = LITERAL.search(line)
        if m:
            registers[m.group(1)] = m.group(2)
            continue
        m = CALLX.search(line)
        if m and m.group(1) in registers:
            graph.calls[name].add(registers[m.group(1)])
    return graph


def disassemble(elf: Path, toolchain: Path) -> list[str]:
    objcopy = toolchain / "xtensa-esp32-elf-objcopy"
    objdump = toolchain / "xtensa-esp32-elf-objdump"
    with tempfile.TemporaryDirectory() as tmp:
        bare = Path(tmp) / "code.elf"
        # The property sections mark LTO-built code as data; without them it decodes.
        subprocess.run([str(objcopy), "-R", ".xt.prop", "-R", ".xt.lit", str(elf), str(bare)], check=True)
        out = subprocess.run([str(objdump), "-d", "--no-show-raw-insn", str(bare)], check=True, capture_output=True, text=True)
    return out.stdout.splitlines()


def size_of(expr: str) -> int:
    if not re.fullmatch(r"[\d\s*]+", expr):
        raise ValueError(f"not a size: {expr!r}")
    total = 1
    for factor in expr.split("*"):
        total *= int(factor)
    return total


def source_size(path: str, pattern: str, root: Path = ROOT) -> int:
    match = re.search(pattern, (root / path).read_text())
    if match is None:
        raise ValueError(f"{path}: no match for {pattern}")
    return size_of(match.group(1))


@dataclass
class TaskCheck:
    task: str
    stack: tuple[str, str]  # file, regex for its size
    stages: list[str]  # outermost first; each a regex over the mangled names


# src/WebServer.cpp: the web server's save callback, which starts the save task,
# and the work it hands that task.
SAVE_CALLBACK = r"^_ZNSt17_Function_handlerIFbRKSt8functionIFbRKN3SQM6ConfigERS2_EEEZNS1_9WebServerC.*_M_invoke"
SAVE_TASK_WORK = r"^_ZNSt17_Function_handlerIFvvEZZN3SQM9WebServerC.*_M_invoke"
EDIT_LAMBDA = r"^_ZNSt17_Function_handlerIFbRKN3SQM6ConfigERS1_E.*_M_invoke"
EDIT_WRAPPER = r"^_ZNSt17_Function_handlerIFbRKN3SQM6ConfigERS1_EZNS0_9WebServer16editAsFlashWrite.*_M_invoke"

TASK_CHECKS = [
    TaskCheck(
        "config_save (every web settings save)",
        ("include/SaveTask.h", r"STACK_BYTES = ([\d\s*]+);"),
        [
            r"^_ZN3SQM8SaveTask.*5entryEPv",
            SAVE_TASK_WORK,
            r"^_Z20updateConfigCallback",  # src/main.cpp: the save itself
            EDIT_WRAPPER,
            EDIT_LAMBDA,
        ],
    ),
    TaskCheck(
        "backup_job (a backup, a restore preview)",
        ("src/WebServerBackup.cpp", r"BACKUP_JOB_STACK_BYTES = ([\d\s*]+);"),
        [r"^_ZN3SQM9WebServer12runBackupJobEPv"],
    ),
]

# Functions on async_tcp that must not reach the save path directly.
HANDLERS = r"(^_ZNK?3SQM9WebServer|^_ZNSt17_Function_handlerIFvP21AsyncWebServerRequest.*9WebServer)"
NOT_HANDLERS = r"(runBackupJob|previewRestore|makeBackupFile|runSiteJob)"
SAVE_PATH = [
    r"^_ZN3SQM6Config9applyJsonE",
    r"^_ZNK3SQM6Config4saveEv",
    r"^_ZNK3SQM6Config8validateE",
]


def check_task(graph: Graph, check: TaskCheck, root: Path, show_path: bool) -> tuple[bool, list[str]]:
    stack = source_size(*check.stack, root=root)
    above = 0
    worst = 0
    worst_path: list[str] = []
    found_any = False
    for stage in check.stages:
        names = graph.names(stage)
        if stage == EDIT_LAMBDA:
            names = [n for n in names if not re.search(EDIT_WRAPPER, n)]
        if not names:
            continue  # inlined into the stage above
        found_any = True
        for name in names:
            depth, path = graph.depth(name)
            if above + depth > worst:
                worst, worst_path = above + depth, path
        above += max(graph.frame.get(n, 0) for n in names)
    if not found_any:
        return False, [f"  {check.task}: none of its functions found in the build"]
    need = worst + MARGIN_BYTES
    ok = need <= stack
    lines = [f"  {check.task:42} deepest {worst:>6,} B + margin {MARGIN_BYTES:,} B = {need:>6,} B of {stack:,} B"]
    if show_path or not ok:
        lines += [f"      {graph.frame.get(n, 0):>5} {n}" for n in worst_path]
    return ok, lines


def check_off_async_tcp(graph: Graph) -> tuple[bool, list[str]]:
    targets = {n for pattern in SAVE_PATH for n in graph.names(pattern)}
    lines = []
    # The web server's save callback hands the save to its own task (SaveTask::run, perhaps inlined).
    starters = set(graph.names(r"^_ZN3SQM8SaveTask3run")) | {"xTaskCreatePinnedToCore"}
    callbacks = graph.names(SAVE_CALLBACK)
    if not callbacks:
        lines.append("  the web server's save callback (src/WebServer.cpp) isn't in the build")
    for name in callbacks:
        if not graph.reaches(name, starters, set()):
            lines.append(f"  {name} doesn't start the save task (SaveTask::run)")
    for name in graph.names(HANDLERS):
        if re.search(NOT_HANDLERS, name) or re.search(r"^_ZNSt17_Function_handlerIFbRKN3SQM6Config", name):
            continue
        path = graph.reaches(name, targets, set())
        if path:
            lines.append("  " + " -> ".join(path))
    return not lines, lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--elf", type=Path, default=ELF_PATH)
    parser.add_argument("--toolchain", type=Path, default=Path(os.environ.get("XTENSA_TOOLCHAIN", TOOLCHAIN)))
    parser.add_argument("--path", action="store_true", help="print each worst path")
    args = parser.parse_args(argv)
    if not args.elf.exists():
        print(f"stack_depth.py: no build at {args.elf}; run `pio run -e esp32dev` first", file=sys.stderr)
        return 1
    graph = parse(disassemble(args.elf, args.toolchain))
    ok = True
    print("Deepest stack of the tasks a settings save runs on (fix 041):")
    for check in TASK_CHECKS:
        passed, lines = check_task(graph, check, ROOT, args.path)
        ok &= passed
        print("\n".join(lines))
    passed, lines = check_off_async_tcp(graph)
    if not passed:
        print("The save path can run on async_tcp (it must go through updateConfigCallback, on the save task):", file=sys.stderr)
        print("\n".join(lines), file=sys.stderr)
    ok &= passed
    if not ok:
        print(
            "STACK-01: a settings save could overflow its task's stack. Move the work onto the save task, "
            "make the path shallower, or raise the task's stack (and record it in the heap budget).",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
