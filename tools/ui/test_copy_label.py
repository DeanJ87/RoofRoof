"""Tests for the English copy check (DS-COPY) and the one-name check (DS-LABEL), SQMeter spec 026."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(f"{name}.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


copy_check = load("copy_check")
label_check = load("label_check")


def write(root: Path, rel: str, data) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data if isinstance(data, str) else json.dumps(data), encoding="utf-8")


class CopyTests(unittest.TestCase):
    def run_check(self, english: dict, context: dict, exceptions: dict | None = None) -> list:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, copy_check.ENGLISH, english)
            write(root, copy_check.CONTEXT, context)
            if exceptions is not None:
                write(root, copy_check.EXCEPTIONS, exceptions)
            return copy_check.check(root)

    def test_lengths_by_type(self):
        problems = self.run_check(
            {"a.pill": "Not responding at all", "a.note": "Fine.", "a.hint": "x" * 161, "a.ok": "Live"},
            {
                "a.pill": "a badge",
                "a.note": "a sentence shown under a control",
                "a.hint": "a help text shown with a '?' tip",
                "a.ok": "a badge",
            },
        )
        self.assertEqual(sorted(p.split(":")[0] for p in problems), ["a.hint", "a.pill"])

    def test_banned_phrases_and_run_ons(self):
        problems = self.run_check(
            {"a.x": "At a glance", "a.y": "Live · Safe · Sending", "a.z": "Please note the limit."},
            {"a.x": "a label", "a.y": "a note", "a.z": "a note"},
        )
        self.assertTrue(any("'at a glance'" in p for p in problems))
        self.assertTrue(any("a.y" in p and "run-on" in p for p in problems))
        self.assertTrue(any("'please note'" in p for p in problems))

    def test_labels_joined_with_a_dash(self):
        problems = self.run_check(
            {"a.old": "Silent for - safety monitor", "a.new": "Safety monitor silent for", "a.note": "Rain held - clears in 5 min"},
            {"a.old": "a field or control label", "a.new": "a field or control label", "a.note": "a note"},
        )
        self.assertEqual([p.split(":")[0] for p in problems], ["a.old"])

    def test_placeholders_count_as_short_values_and_device_text_is_skipped(self):
        problems = self.run_check(
            {"a.p": {"one": "{count} to check", "other": "{count} to check"}, "device.x": "x" * 300},
            {"a.p": "a badge", "device.x": "a sentence"},
        )
        self.assertEqual(problems, [])

    def test_exceptions_need_a_reason_and_a_key(self):
        problems = self.run_check({"a.l": "A much longer label than allowed here"}, {"a.l": "a label"}, {"a.l": "", "gone": "why"})
        self.assertTrue(any("needs a reason" in p for p in problems))
        self.assertTrue(any("gone" in p for p in problems))
        self.assertEqual(
            self.run_check({"a.l": "A much longer label than allowed here"}, {"a.l": "a label"}, {"a.l": "EXC-01: fixed name"}), []
        )


class LabelTests(unittest.TestCase):
    def run_check(self, english: dict, docs: dict, entities: str = "", context: dict | None = None) -> list:
        glossary = {
            "names": [
                {"name": "Park sensor", "keys": ["sensor.park"], "avoid": ["A3144 Park Sensor"], "parts": ["A3144"]},
                {"name": "Alerts", "keys": [], "avoid": [], "ha": "Alerts"},
            ]
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, label_check.GLOSSARY, glossary)
            write(root, label_check.ENGLISH, english)
            write(root, label_check.HA_DISCOVERY, entities)
            if context is not None:
                write(root, label_check.CONTEXT, context)
            for rel, text in docs.items():
                write(root, rel, text)
            return [message for _, message in label_check.check(root)]

    def test_one_name(self):
        problems = self.run_check({"sensor.park": "Park Sensor", "system.x": "A3144 Park Sensor"}, {}, '{"switch", "alerts", "Alerts"}')
        self.assertEqual(len(problems), 1)
        self.assertIn("system.x", problems[0])

    def test_prose_and_hardware_pages_and_device_text_pass(self):
        problems = self.run_check(
            {"sensor.park": "Park sensor", "device.x": "A3144 Park Sensor"},
            {"docs/guide.md": "the A3144 park sensor", "docs/hardware/bom.md": "A3144 Park Sensor"},
            '"Alerts"',
        )
        self.assertEqual(problems, [])

    def test_card_titles_and_device_reasons_use_the_name(self):
        problems = self.run_check(
            {
                "sensor.park": "Park sensor",
                "dashboard.t": "A3144 Readings",
                "device.alert.f": "Sensor fault: A3144 park",
                "device.alert.g": "A3144 Park Sensor recovered",
                "device.api.x": "A3144 not detected",
                "system.row": "A3144",
            },
            {},
            '"Alerts"',
            {"dashboard.t": "Dashboard: a card or section title; max 20 chars."},
        )
        self.assertEqual(sorted(p.split(" ")[0] for p in problems), ["dashboard.t", "device.alert.f", "device.alert.g", "device.alert.g"])

    def test_dependency_reasons_use_the_name(self):
        # Why a setting isn't in effect is status text: the thing, not the part.
        problems = self.run_check(
            {
                "sensor.park": "Park sensor",
                "settingsDeps.hallMissing": "A3144 not detected",
                "settingsDeps.ok": "Park sensor not detected",
            },
            {},
            '"Alerts"',
        )
        self.assertEqual([p.split(" ")[0] for p in problems], ["settingsDeps.hallMissing"])

    def test_docs_and_home_assistant(self):
        problems = self.run_check({"sensor.park": "Park sensor"}, {"docs/guide.md": "the A3144 Park Sensor"})
        self.assertTrue(any("docs call" in p for p in problems))
        self.assertTrue(any("Home Assistant" in p for p in problems))


if __name__ == "__main__":
    unittest.main()
