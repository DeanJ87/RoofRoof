# Translations

The web UI is English only for now (spec 001), but the machinery for adding a language later is kept and checked, so a language is one more file rather than a rework. This page is for contributors: how text gets into the UI and what the checks enforce.

## How it fits together

- **English is built in**: `web/src/i18n/en.json` is bundled with the UI and is the source for every key, with a context note per key in `en.context.json`.
- **Text goes through `t()`**: components never hold literal user-visible strings. `node tools/i18n/literals.mjs` fails on an untranslated literal in a component.
- **The firmware's messages** are put into the catalogue by `python3 tools/i18n/gen_device_catalog.py` (CI runs it with `--check`).
- **Checks**: `node tools/i18n/check.mjs` (the catalogue is complete and consistent) and `python3 tools/i18n/context.py --check` (every key has a context note) run in CI.
- **Machine formats are never localised**: the REST API, WebSocket, MQTT and Alpaca output are the same in every language - numbers use `.` as the decimal point, and field names and units are never translated.

## Adding a language later

There are no language packs, no download on the device and no translation tooling in this release. When a language is added, it will come with its own spec: a file next to `en.json`, an entry in `languages.ts`, and the checks above extended to it.
