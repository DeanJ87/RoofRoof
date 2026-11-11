# Tasks: Daylight Failsafe

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: the pure logic first (daylight state, sequence, gates, alerts, settings), then
firmware, demo, web and docs. Every task ends with `pio test -e native` green; firmware tasks also
with `pio run -e esp32dev`.

## Phase 1: Setup

- [ ] T001 Add "013" to `specs/ROADMAP.md`; create `lib/DaylightLogic` with SQMeter's `SunPosition.h/.cpp` (877208b, unchanged)

## Phase 2: Tests first

- [ ] T002 [US1] [US4] `test/test_daylight`: SQMeter's sun position reference values; the state table (off, unknown location, unknown clock, night, day), the band edges (limit - 0.5° - ε, limit - 0.5°, limit - ε, limit), band reset on mode change and unknown, forced sun up, texts (FR-005), `closes()` only for close + day, settings from config
- [ ] T003 [US2] [US3] `test/test_close_daylight`: daylight close requests the park, waits for it, closes; park timeout alerts with daylight wording; person's stop stands down and stays while the sun is up; re-arm after 10 min without a reason; halt stays; weather turning unsafe re-arms a daylight stand-down, halt and back-off on the same step (not with a person's stop on that step); waiting and closing carry on; episode flips to weather; a weather stand-down stays while daylight wants a close; mode change ends a waiting attempt; restart keeps the daylight flag; decisions carry `daylight`
- [ ] T004 [US2] [US3] Same suite: randomised runs (≥ 5,000 each): (a) daylight false vs. a second sequence fed the same inputs with daylight from mode off/alert: identical decisions (SC-002); (b) with daylight: never Close without the park held, never an open, no re-arm from HeldByPerson/Halted while the weather stays safe and daylight wants a close, weather close starts within one step after a daylight stand-down (SC-003)
- [ ] T005 [US2] `test_roof_gates`, `test_roof_status`, `test_dome_bridge`: daylight refusal only with `daylightCloses` and the weather safe; weather wins; `openMustStop` with daylight; refusal text; status object; Alpaca error text
- [ ] T006 [US1] [US4] `test_roof_alert_events`, `test_roof_alerts`: daylight open (settle 29/30 s, not while a weather episode runs, once per episode), daylight unknown (settle 599/600 s), daylight closed per count, announced bits 4 and 5; conditions from the view and the sequence; unsafe closing and roof closed for weather episodes only
- [ ] T007 [US5] `test_config_*`, `test_roof_alert_settings`, `test_settings_deps`: defaults, ranges, an old config loads as alert, round trip, the three event settings, settings deps entries

## Phase 3: Pure logic

- [ ] T008 `lib/DaylightLogic` until T002 passes
- [ ] T009 `lib/CloseSequenceLogic` (sequence, text, RoofAlerts) until T003-T004 pass; every spec 005 test unchanged
- [ ] T010 `lib/RoofIoLogic` until T005 passes
- [ ] T011 `lib/AlertLogic`, `lib/DeviceCore` until T006 passes; `template-variables.json`
- [ ] T012 `lib/ConfigModel`, `lib/SettingsDeps` until T007 passes

## Phase 4: Firmware

- [ ] T013 `CloseOnUnsafe`: owns the watch, settings under a lock, daylight into the sequence and conditions, the RTC flag; `main.cpp` hands daylight to the gates; `RoofControl::setSafety`; `RoofControlTask` stop notice; status `roof.daylight`
- [ ] T014 Zero warnings, flash budget, `tools/alpaca-sim` builds

## Phase 5: Demo and web

- [ ] T015 [US5] Demo core: watch, "Sun up" injector, status; `bash tools/demo-core/build.sh`
- [ ] T016 [US5] Web: types, config schema and ranges, Roof card line, Open reason, Settings → Roof Daylight card, alert rows, defaults, variables, settings deps labels, mocks, i18n; demo panel switch; Vitest
- [ ] T017 [US5] Playwright: the Daylight card saves; the demo "Sun up" closes the roof and Open shows the daylight reason
- [ ] T018 Contracts and OpenAPI regenerated (`tools/contracts/generate_schemas.py`, `tools/api/openapi.py`)

## Phase 6: Docs

- [ ] T019 `docs/user-guide/daylight.md`, configuration, alerts, close-on-unsafe, REST, settings dependencies, heritage, CHANGELOG; mkdocs nav; diagram re-check

## Phase 7: Verify

- [ ] T020 Quickstart §1 of spec 001; converge; independent reviews until one says safe to merge
