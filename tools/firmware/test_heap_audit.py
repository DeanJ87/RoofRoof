"""Spec 031: replies are never written into a growing string.

The firmware has no C++ exceptions, so a std::string, std::vector or `new`
that can't get memory calls abort(). Under six web clients the heap fragmented
until `serializeJson(doc, std::string&)` could not double its text and the
device restarted about eleven times in five minutes (spec 031 research R1).

Replies go through src/WebReply.cpp: `serializeJson` into a HeapSafe::ChunkText
(1 KB pieces from malloc, each checked), a HeapSafe::BufferedWriter (MQTT,
straight to the broker), a JsonStream, or a fixed char buffer. WebSocket text
goes out through WebReply::textAll/text, which checks the heap first.

Anything else must be listed in ALLOWED with the reason it is safe.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# "path: destination" -> why it is safe.
ALLOWED: dict[str, str] = {
    "src/WebServerApi.cpp: body": "the settings save's request body, re-serialized for the save checks; refused with 503 "
    "below a 20 KB block and 48 KB free (spec 029 R13)",
    "lib/AlpacaLogic/src/RouterInternal.h: response.body": "an Alpaca reply of a few hundred bytes, reserved once from "
    "measureJson (no growth)",
    "lib/DiagLogic/src/DiagMoves.cpp: text": "one move summary (at most SUMMARY_DOC_CAPACITY's text), reserved once from "
    "measureJson, a part of the chunked moves list",
    "lib/DeviceCore/src/AlertDelivery.cpp: out": "an outgoing alert message (768-byte document) on the alert task, not a " "reply",
    "lib/ConfigModel/src/ConfigJsonOut.cpp: json": "the stored settings text for NVS; saves are refused while memory is "
    "short (spec 029 R13)",
    "lib/ConfigModel/src/ConfigJsonOut.cpp: output": "the settings text for the save checks and backups, guarded as above",
    "lib/ConfigBackup/src/BackupRules.cpp: out": "a backup, built one request at a time after its own free-memory check " "(spec 024)",
    "lib/ConfigBackup/src/BackupFile.cpp: out": "a backup file, guarded as above",
    "lib/ConfigBackup/src/BackupFile.cpp: out.settings": "a backup's settings, guarded as above",
    "lib/ConfigBackup/src/BackupPreview.cpp: value": "a backup preview's field, guarded as above",
    "lib/ConfigBackup/src/BackupPreview.cpp: applyText": "a backup preview's settings, guarded as above",
    "lib/ConfigBackup/src/BackupPreview.cpp: out": "a backup preview, guarded as above",
}

SAFE_TYPES = r"(?:HeapSafe::)?(?:ChunkText|InnerWriter)|(?:HeapSafe::)?BufferedWriter\s*<[^>]*>"


def sources():
    for folder in ("src", "include", "lib"):
        for p in sorted((ROOT / folder).rglob("*")):
            if p.suffix in {".cpp", ".h"} and "/test" not in p.relative_to(ROOT).as_posix():
                yield p


def call_args(text: str, start: int) -> list[str]:
    """The top-level arguments of the call whose '(' is at text[start]."""
    depth, args, current = 0, [], ""
    for ch in text[start:]:
        if ch in "([{":
            depth += 1
            if depth == 1:
                continue
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                args.append(current.strip())
                return args
        if ch == "," and depth == 1:
            args.append(current.strip())
            current = ""
        else:
            current += ch
    return args


def safe_names(text: str) -> set[str]:
    names = set(re.findall(rf"\b(?:{SAFE_TYPES})\s*&?\s*(\w+)\b", text))
    names |= set(re.findall(r"\bchar\s+(\w+)\s*\[", text))
    return names


def companion(path: Path) -> str:
    """The header that declares a source's members (lib/X/include, include/)."""
    out = ""
    for candidate in (path.parent.parent / "include" / f"{path.stem}.h", ROOT / "include" / f"{path.stem}.h"):
        if candidate.exists() and candidate != path:
            out += candidate.read_text(encoding="utf-8")
    return out


def _blank(match: re.Match) -> str:
    return re.sub(r"[^\n]", " ", match.group(0))


def without_comments(text: str) -> str:
    """Comments blanked out, line numbers kept."""
    return re.sub(r"//[^\n]*|/\*.*?\*/", _blank, text, flags=re.S)


HELPER = {"src/WebReply.cpp", "include/WebReply.h"}


def findings_in(rel: str, text: str, declared: str = "") -> list[str]:
    out = []
    text = without_comments(text)
    names = safe_names(text + declared)
    for m in re.finditer(r"\bserializeJson\s*\(", text):
        args = call_args(text, m.end() - 1)
        if len(args) != 2:
            continue  # (doc, buffer, size): a fixed buffer
        dest = args[1].lstrip("*&").strip()
        if dest in names:
            continue
        key = f"{rel}: {dest}"
        if key not in ALLOWED:
            out.append(f"{key} (line {text.count(chr(10), 0, m.start()) + 1}): serializeJson into a growing string")
    if rel not in HELPER:
        for m in re.finditer(r"(?<!WebReply::)\b(textAll|text)\s*\(", text):
            before = text[max(0, m.start() - 2) : m.start()]
            if m.group(1) == "textAll" or before == "->":
                line = text.count("\n", 0, m.start()) + 1
                out.append(f"{rel}: {m.group(1)} (line {line}): WebSocket text outside WebReply::textAll/text")
    return out


def findings() -> list[str]:
    out = []
    for path in sources():
        rel = path.relative_to(ROOT).as_posix()
        out.extend(findings_in(rel, path.read_text(encoding="utf-8", errors="replace"), companion(path)))
    return out


# Spec 035: an allocation on a running path is checked (new (std::nothrow)),
# so a short heap skips or fails one step instead of aborting. make_unique
# can't be checked; these are made once at start-up. "path" -> why.
MAKE_UNIQUE_ALLOWED: dict[str, str] = {
    "src/main.cpp": "the services, made once in setup()",
    "src/WebServer.cpp": "the alert dispatcher, made once in begin()",
    "src/MQTTClient.cpp": "the MQTT client, made once with its owner",
}
# The full settings-dependency list (about 40 entries) is for the settings
# route; the alert path asks Deps::alertChannels, which allocates nothing.
EVALUATE_ALLOWED = {"src/WebServerApi.cpp"}


def running_path_findings(rel: str, text: str) -> list[str]:
    out = []
    text = without_comments(text)
    for m in re.finditer(r"\bmake_unique\s*<", text):
        if rel.startswith("src/") and rel not in MAKE_UNIQUE_ALLOWED:
            out.append(f"{rel} (line {text.count(chr(10), 0, m.start()) + 1}): make_unique on a running path (use new (std::nothrow))")
    for m in re.finditer(r"\bDeps::evaluate\s*\(", text):
        if rel.startswith("src/") and rel not in EVALUATE_ALLOWED:
            out.append(f"{rel} (line {text.count(chr(10), 0, m.start()) + 1}): the full settings list outside the settings route")
    return out


class HeapAuditTest(unittest.TestCase):
    def test_running_paths_check_their_allocations(self):
        out = []
        for path in sources():
            rel = path.relative_to(ROOT).as_posix()
            out.extend(running_path_findings(rel, path.read_text(encoding="utf-8", errors="replace")))
        self.assertEqual(out, [])

    def test_flags_the_alert_path_that_crashed(self):
        bad = "ChannelBlocks W::channelBlocks() const { const auto e = Deps::evaluate(cfg, f); auto j = std::make_unique<Job>(); }"
        self.assertEqual(len(running_path_findings("src/WebServerStatus.cpp", bad)), 2)
        self.assertEqual(running_path_findings("src/main.cpp", "auto w = std::make_unique<WiFiManager>();"), [])

    def test_no_reply_is_written_into_a_growing_string(self):
        self.assertEqual(findings(), [])

    def test_every_exception_is_still_needed(self):
        seen = set()
        for path in sources():
            rel = path.relative_to(ROOT).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            for m in re.finditer(r"\bserializeJson\s*\(", text):
                args = call_args(text, m.end() - 1)
                if len(args) == 2:
                    seen.add(f"{rel}: {args[1].lstrip('*&').strip()}")
        self.assertEqual(sorted(set(ALLOWED) - seen), [])

    def test_flags_the_pattern_that_crashed(self):
        bad = "void f(){ std::string json; serializeJson(doc, json); request->send(200, t, json.c_str()); }"
        self.assertEqual(len(findings_in("src/X.cpp", bad)), 1)
        bad_ws = "void g(){ wsStatus.textAll(json.c_str()); client->text(json.c_str()); }"
        self.assertEqual(len(findings_in("src/X.cpp", bad_ws)), 2)

    def test_accepts_the_safe_forms(self):
        good = (
            "void f(HeapSafe::ChunkText &out){ serializeJson(doc, out); char buf[64]; serializeJson(doc, buf);"
            " serializeJson(doc, raw, 64); HeapSafe::BufferedWriter<PubSubClient> writer(c); serializeJson(doc, writer);"
            " WebReply::textAll(ws, text); WebReply::text(client, text); }"
        )
        self.assertEqual(findings_in("src/X.cpp", good), [])


if __name__ == "__main__":
    unittest.main()
