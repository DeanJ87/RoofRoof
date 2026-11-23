# Tasks: Test Box Bypass

**Input**: spec.md, plan.md, research.md, data-model.md, contracts/test-box.md

## Phase 1: Setup

- [x] T001 Constitution 3.0.0 (Principles II and III, Sync Impact Report) in `.specify/memory/constitution.md`; roadmap entry for 022 in `specs/ROADMAP.md`

## Phase 2: Settings and pure logic (US1-US4) - tests first

- [x] T002 [P] Native tests `test/test_test_box`: settings defaults, JSON in/out (`until` never from a strict body), validation (exclusive with N.I.N.A. and SQMeter, hours range), the stamp (on, renew, off, clock unset), in effect (deadline boundary, clock, exclusivity), the weather and park effects (rain contact, switches, N.I.N.A. configured), the hold, texts, bits
- [x] T003 `TestBoxConfig` and `AlertsConfig::testBox` in `lib/ConfigModel` (Config.h, ConfigModel.cpp defaults, ConfigJsonIn/Out, ConfigValidate, ConfigInternal events), the stamp in `lib/ConfigModel/src/TestBoxSave.cpp`
- [x] T004 `lib/SafetyInputLogic/include/TestBox.h`, `src/TestBox.cpp`: state, apply to verdict/park/hold, status text, bits, Dome prefix; `bypassed` on `WeatherVerdict` and `ParkResult` with their texts
- [x] T005 [P] Daylight bypass in `lib/DaylightLogic` (`Settings::testBox`, `View::bypassed`, `closes()`, describe) with `test_daylight` cases
- [x] T006 [P] `RoofIo::TestBoxStatus` in the snapshot and `writeStatus` (`lib/RoofIoLogic`), `test_roof_status` and the MQTT document fit
- [x] T007 [P] `AlertType::TestBox`: engine (`RoofInputs::testBox`, new-bit rule, grace), wording, rules, `DeviceAlerts`/`AlertDelivery` tables; `test_roof_alert_events` cases; level never off (validate, raise on load)
- [x] T008 [P] Dome description prefix (`Alpaca::domeDeviceInfo`) with `test_alpaca_description`; Home Assistant `test_box` binary sensor with `test_ha_discovery`
- [x] T009 [P] Settings-deps entries (`testBox.*`, `alerts.events.test_box.level`) in `lib/SettingsDeps`, catalogue, `web/src/lib/settingsDeps.ts`, fixtures
- [x] T010 Fault suites with every bypass on (`test_test_box`: gates with bypass; a fault from each source still latches)

## Phase 3: Firmware glue and demo core

- [x] T011 `SafetyInputs`: test-box settings and the clock; `applyTestBox` after evaluation; log on change
- [x] T012 `CloseOnUnsafe`: daylight `testBox` flag each step; the alert conditions' `testBox` bits and text
- [x] T013 `WebServerApi`: the stamp inside the save lock (clock from `time()`); `WebServerStatus`: `roof.testBox`; `WebServerAlpaca`: the description; `main`: boot warning
- [x] T014 Demo core: `EmulatedRoof` applies the same functions; the demo save stamps `until`; status and description; `bash tools/demo-core/build.sh`

## Phase 4: Web UI (US1, US2, US5)

- [x] T015 Types, zod schema, `test/fixtures/config-ranges.json`, defaults, MSW mocks
- [x] T016 `TestBoxCard` in Settings → Roof (three toggles, hours, deadline, Renew, warning), i18n
- [x] T017 `TestBoxBanner` beside the bench banner (polls 5 s and on a save), dashboard park and weather pills "Test box bypass", daylight row
- [x] T018 Wizard `testBox` screen on the Hardware page; weather and N.I.N.A. screens skipped; review line
- [x] T019 Alerts: `test_box` event row (no Off), wording
- [x] T020 Vitest for each; Playwright test-box spec; dashboard inventory; screenshots if the suite takes them

## Phase 5: Docs and contracts

- [x] T021 `docs/user-guide/test-box.md` (new, in nav); configuration, REST, MQTT/Home Assistant, Alpaca, safety inputs, daylight, alerts and wizard pages; CHANGELOG
- [x] T022 OpenAPI (`python3 tools/api/openapi.py`), contract schemas (`tools/contracts/generate_schemas.py`), i18n device catalogue (`tools/i18n/gen_device_catalog.py`)

## Phase 6: Verify

- [x] T023 spec 001 quickstart §1: firmware build, native tests, web typecheck/test/build, Playwright, quality gate, flash budget, docs build; `source_hash.py --check`
- [x] T024 Converge; review rounds until safe to merge; PR
