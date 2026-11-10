"""Tests for tools/i18n/gen_device_catalog.py and context.py (specs/023-i18n).

python3 -m unittest discover -s tools/i18n -p 'test_*.py'
"""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import context  # noqa: E402 - imported after the sys.path insert above
import gen_device_catalog as catalog  # noqa: E402 - imported after the sys.path insert above


class DeviceCatalogTest(unittest.TestCase):
    def test_every_source_exists(self):
        # A source the firmware no longer has would stop the generator: trim the list with the firmware.
        missing = [rel for rel in [*catalog.SOURCES, *catalog.ASSIGN, *catalog.TABLES] if not (catalog.ROOT / rel).exists()]
        self.assertEqual(missing, [])

    def test_concatenation_becomes_named_placeholders(self):
        self.assertEqual(catalog.template('"Port " + String(port) + " is in use"', {}), "Port {port} is in use")

    def test_printf_becomes_named_placeholders(self):
        self.assertEqual(
            catalog.from_printf("Silence time %d to %d s", ["minSeconds", "maxSeconds"]), "Silence time {minSeconds} to {maxSeconds} s"
        )

    def test_identifiers_and_file_names_are_not_text(self):
        self.assertFalse(catalog.has_words("config.json"))
        self.assertTrue(catalog.has_words("Not RoofRoof firmware."))

    def test_calls_and_assignments_are_found(self):
        src = 'createErrorJson("Restore refused: wrong product"); doc["error"] = ok ? "Saved" : "Couldn\'t save the settings";'
        found = catalog.from_calls(src, {"createErrorJson": [0]}) + catalog.from_assignments(src, catalog.ASSIGN["src/WebServer.cpp"])
        self.assertIn("Restore refused: wrong product", found)
        self.assertIn("Couldn't save the settings", found)

    def test_existing_keys_are_kept_and_stale_ones_dropped(self):
        en = {"device.api.oldName": "Restore refused: wrong product", "device.api.gone": "Gone", "common.save": "Save"}
        found = [("api", "Restore refused: wrong product", "src/WebServerBackup.cpp")]
        original = catalog.collect
        catalog.collect = lambda: found
        try:
            new_en, new_ctx, _ = catalog.build(en, {"device.api.gone": "old note"})
        finally:
            catalog.collect = original
        self.assertEqual(new_en, {"common.save": "Save", "device.api.oldName": "Restore refused: wrong product"})
        self.assertEqual(list(new_ctx), ["device.api.oldName"])


class ContextTest(unittest.TestCase):
    def test_place_is_the_longest_known_prefix(self):
        self.assertEqual(context.place_of("settings.alerts.silentFor"), "Settings -> Alerts")
        self.assertEqual(context.place_of("settings.save"), "Settings")
        self.assertEqual(context.place_of("unknown.key"), "Web UI")

    def test_note_names_the_control_and_a_length_limit(self):
        # The code around the call, as in a real component: a few lines before it.
        source = "// Settings\n" + " " * 250 + "<Button onClick={save}>{t('settings.save')}</Button>"
        note = context.note_for("settings.save", "Save", {"Settings.tsx": source})
        self.assertTrue(note.startswith("Settings: a button"), note)
        self.assertIn("max 12 chars", note)

    def test_note_explains_kept_terms(self):
        self.assertIn("keep 'Alpaca'", context.note_for("alpaca.title", "Alpaca", {}))

    def test_every_place_prefix_has_keys(self):
        # Places for pages RoofRoof doesn't have are left over from SQMeter.
        en = json.loads(context.EN.read_text(encoding="utf-8"))
        unused = [place for place in context.PLACES if not any(key == place or key.startswith(place + ".") for key in en)]
        self.assertEqual(unused, [])


if __name__ == "__main__":
    unittest.main()
