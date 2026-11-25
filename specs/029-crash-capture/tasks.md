# Tasks: Crash Capture

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).
Python tool tests with pytest; web logic in Vitest; the page in Playwright.

**Organisation**: the pure logic first (ring, record, documents, alert), then firmware, tools and
CI, demo and web, docs.

## Phase 1: Setup

- [X] T001 Add "029" to `specs/ROADMAP.md`; create `lib/CrashLogic` (`library.json`, headers per data-model.md); add `-Ilib/CrashLogic/include` to `[env:native]`

## Phase 2: Tests first

- [X] T002 [US4] `test/test_crash_ring`: the check value (known FNV-1a vectors); `start` empties after a bad magic or check, keeps and bumps the boot when valid, empties with `keep` false; `push` cuts at 71 characters, drops ESC colour sequences, control characters and trailing newlines, keeps the 50 newest, oldest first; a damaged line is skipped by `copy`; `tail` returns the newest N lines of earlier boots only; head and count out of range empty the ring at `start`
- [X] T003 [US1] `test/test_crash_record`: kinds and tokens for every reset code (0-15 and an unknown); `build` from a panic capture (fields copied, frames ≤ 16, flags), a task watchdog capture with tasks, a brown-out without capture (snapshot only, reason wording), a panic whose capture is damaged (`captured` false); the epoch from the snapshot's clock and uptimes, none without a clock; the log tail (8); `encode`/`decode` round trip; `decode` refuses another size or version; `slot` rotation keeps the newest 3; `initialAnnounced` for kept, power-on with and without a record
- [X] T004 [US1] [US3] [US4] `test/test_crash_documents`: the records document (keys, nulls without capture or snapshot, hex addresses, `decode` string, newest first, empty list); the log document (levels, boots, oldest first); the device message (token, code, boot count, crash summary or null); the alert reason and the count wording
- [X] T005 [US3] `test/test_alert_engine` (or the roof alerts test): a new restart sequence alerts once after the grace with the reason and count; not during the grace; not again for the same sequence; off follows silently; `announcedRestart`/`restoreAnnouncedRestart` (withdrawn for flash: T034)
- [X] T006 [US3] `test/test_ha_discovery`: the "Last restart reason" sensor (diagnostic, `<base>/device`, no state class); removed with discovery off (withdrawn for flash: T034)
- [X] T007 [US3] Config tests: `alerts.events.restart` default level 2, round trip, range 0-4; config-ranges fixture; settings-deps fixtures (withdrawn for flash: T034)

## Phase 3: Pure logic

- [X] T008 `lib/CrashLogic` check, `LogRing` until T002 passes
- [X] T009 `lib/CrashLogic` `Record`, kinds, tokens, `build`, blob, slots, `initialAnnounced` until T003 passes
- [X] T010 `lib/CrashLogic` documents and alert text until T004 passes
- [X] T011 `lib/AlertLogic` `AlertType::Restart`, rule, engine announcement and keep accessors until T005 passes; `template-variables.json` (withdrawn for flash: T034)
- [X] T012 `lib/ConfigModel` `restart` event setting (Config.h within 600 lines); `lib/DeviceCore` rule, level table and icon; `lib/SettingsDeps` catalogue row until T007 passes (withdrawn for flash: T034)
- [X] T013 `lib/HaDiscovery` restart sensor until T006 passes (withdrawn for flash: T034)

## Phase 4: Firmware (US1, US3, US4)

- [X] T014 `src/CrashLog.cpp` + `include/CrashLog.h`: RTC capture, snapshot, ring and announced sequence; `arm()` (ring start by reset reason, Arduino panic hook, ESP-IDF log hook); `esp_task_wdt_isr_user_handler`; `keepRecord()` (NVS `crashlog`, sequence, slot, capture cleared, failures logged); `snapshot()` once a second; `copyRing`, `loadRecords`, `thisBoot`; `static_assert`s on the reset codes
- [X] T015 `Logger::log` copies each printed line into the ring; `main.cpp`: `CrashLog::arm()` first in `setup()`, `keepRecord()` after `PinSetup::begin` and before `roofControl.begin`, `snapshot()` in `loop()`
- [X] T016 `src/WebServerCrash.cpp`: `GET /api/diag/crashes`, `GET /api/diag/log` (`requireAuth`, from copies, registered before `/api/diag`); the `<base>/device` message and the alert input withdrawn (T034)
- [X] T017 `platformio.ini` `-Wl,--wrap=esp_panic_handler`; `scripts/keep_elf.py` (post: check the image's `.elf` SHA-256 at 0xb0, keep the last 5 gzipped under `.pio/elf/`, skipped in CI); confirm `__wrap_esp_panic_handler` is linked
- [X] T018 `pio run -e esp32dev` and `-e esp32dev_bench` with zero warnings; flash measured and recorded

## Phase 5: Tools and CI (US2)

- [X] T019 `tools/decode_crash.py`: record JSON (file, stdin, the whole document or one record), `--addrs`, `--elf` plain or gzipped, build check and `--force`, addr2line lookup (PATH or PlatformIO toolchain), output per frame; `tools/test_decode_crash.py` with a fake addr2line
- [X] T020 `.github/workflows/build.yml`: upload `firmware.elf` as `firmware-elf`; the release attaches `roofroof-l2-firmware-<tag>.elf.gz`

## Phase 6: Demo and web (US1, US4)

- [X] T021 Demo core: `crashesDocument()` and `logDocument()` from `lib/CrashLogic` (no records, the demo's own lines); `build.sh`; demo handlers
- [X] T022 Web: `types/crash.ts`, `lib/crashApi.ts`, `components/diagnostics/CrashesCard.tsx` on the Diagnostics page (diagnostics on and off): records with pills and rows, backtrace, Copy (JSON), Copy log (text), empty note; i18n with context notes and glossary; mocks; Vitest
- [X] T023 Web: the "Restarted unexpectedly" row in Settings → Alerts (types, defaults, schema, labels, variables, depLabels); Vitest (withdrawn for flash: T034)
- [X] T024 Playwright: the Crashes card with records and empty, Copy, phone width, axe
- [X] T025 `tools/api/routes.json`, `operations.json`, examples, contract schemas (`diag-crashes`, `diag-log`), `docs/api/openapi.json`

## Phase 7: Docs

- [X] T026 `docs/user-guide/diagnostics.md` (Crashes card, log), `alerts.md` (event), `mqtt.md` (`<base>/device`, Home Assistant sensor), `docs/api/rest.md`, `docs/development/crash-decoding.md` (new, `mkdocs.yml`), settings dependencies reference, CHANGELOG

## Phase 8: Field evidence (panics while the Hall input flapped)

- [X] T028 `test/test_hall_flap`: a rail and a mid Hall reading alternating for 1000 s (every step and every 37 steps), and with a person clearing every 2 s: one latch and one NVS write without clears, at most two writes per cycle with clears, the driver off throughout, the record read back as written (research R10)
- [X] T029 The capture keeps the crashing task's stack left (read after a first seal); the snapshot adds the weather poller's and the alert sender's stacks; the record, documents, Crashes card, mocks, schemas and docs follow (research R10)
- [X] T030 Static stack frames of the roof task measured with `-fstack-usage` and recorded in research R10

## Phase 9: Verify

- [X] T027 Quickstart §1 of spec 001 and §1-2 of this spec; demo core source hash; converge

## Phase 10: Convergence

- [X] T031 Clear the RTC capture in `CrashLog::keepRecord()` once the record is stored, not when `arm()` sets it aside, per FR-009 (contradicts)- [X] T032 Field evidence (research R11): audit every interrupt, timer callback and PWM use for code that could run with the flash cache off (none); `tools/firmware/test_isr_audit.py` in CI; the capture records whether a flash write was in progress (`FlashGuard::writingNow`, `flashWrite` in the record and the Crashes card)
- [X] T033 Read the code RoofRoof shares with SQMeter for the panics' cause; record the settings race (one task replaces the global `Config` while others read it unlocked) with its files and functions in research R12 and as an owner assumption; the fix is a separate spec

## Phase 11: Flash budget after spec 027 (research R9)

- [X] T034 Withdraw the restart alert, the `<base>/device` message, the Home Assistant sensor and the ESP-IDF log hook (spec Out of scope); rebuild on main with spec 027
- [X] T035 Plain-text documents written by hand (`crashesText`, `logText`) instead of ArduinoJson, sent whole; the record keeps the capture and snapshot as read; the last lines taken at `arm()`; records read newest-first by sequence; tests, web card, mocks, decode tool, OpenAPI (plain-text responses) and docs follow
- [X] T036 Field evidence (research R13): `GET /api/config` from a checked pool and buffer (`ConfigJson::write`, `503` when short of memory); `POST /api/config` refused with `503` while memory is short; `test/test_config_json_body`; `tools/load_config_get.py`; REST and OpenAPI note the `503`

