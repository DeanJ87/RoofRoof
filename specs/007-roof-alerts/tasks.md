# Tasks: Roof Alerts

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: the pure logic first (engine, mapping, finishing, settings), then firmware, web
and docs. Every task ends with `pio test -e native` green; firmware tasks also with
`pio run -e esp32dev`.

## Phase 1: Setup

- [x] T001 Add the seven alert types in place of `Roof` in `lib/AlertLogic/include/AlertEngine.h` with their names in `AlertEngine.cpp`, ntfy tags in `lib/DeviceCore/src/AlertDelivery.cpp` and `Alerts::isCritical`; update `test_alert_clients`' name check

## Phase 2: Tests first

- [x] T002 [US2] [US3] [US4] `test/test_roof_alert_events/test_main.cpp`: the engine's roof trackers: fault edge, no repeat while latched, again after a clear (cooldown 299/300 s); at least a minute apart with a cooldown of 0; supply and source settle 29/30 s; nothing in the grace, announced when it ends; level off follows silently; closing once per cooldown with a flapping phase; closed per count rise, held through the grace, not by the cooldown; announced bits restore and clear; stacking; `known` false ignores the roof; waits report roof conditions
- [x] T003 [US2] [US3] [US4] Same suite: a randomised property test (≥ 5,000 seeded runs) of roof inputs over time: never twice within the cooldown, never in the grace, never while the level is off, and every condition that held for its settle time plus the cooldown outside the grace is announced (SC-003)
- [x] T004 [US1] `test/test_roof_alerts/test_main.cpp`: `alertTypeFor` for every event; `criticalAlert` carries spec 005's title and message and the `reason`, `limit`, `details`, `reminder` variables; `roofConditions` from snapshots (fault text, supply text and boundary, lost sources and rain contact, no source on, waiting source, attempting phases)
- [x] T005 [US1] `test/test_roof_alert_settings` (new): `finishCriticalAlert`: level floor for 0-4, sound, custom wording rendered, built-in message added without `{reason}`/`{details}`, "Still: " on reminders; rules and settings lookup for every roof event; samples for every roof event (critical ones at urgent or above)
- [x] T006 [US5] Same suite: roof event defaults, round trip, old config, critical level 2/3 refused/accepted, level 5 refused, the settings documents not cut short with every event at full length; `test/fixtures/settings-deps` and `test_settings_deps` cases

## Phase 3: Pure logic

- [x] T007 `lib/AlertLogic/src/RoofEvents.cpp` and `AlertEngine.h` (RoofInputs, rules, trackers, announced bits, waits) until T002-T003 pass
- [x] T008 `lib/CloseSequenceLogic/include/RoofAlerts.h`, `src/RoofAlerts.cpp`; `RoofIo::describeSupply` until T004 passes
- [x] T009 `lib/DeviceCore` (rules, settings lookup, `finishCriticalAlert`, samples) until T005 passes; `template-variables.json` and `test_alert_variables`
- [x] T010 `lib/ConfigModel` (fields, keys, defaults, validation) and `lib/SettingsDeps` (D-04 entries, catalogue) until T006 passes

## Phase 4: Firmware

- [x] T011 `CloseOnUnsafe`: typed critical alerts through the existing sink and resend; the closed count; `conditions()` each step
- [x] T012 `WebServer`: `setRoofConditions`, roof inputs into the engine, RTC announced bits, `raiseRoofAlert` finishing; `main.cpp` hand-over
- [x] T013 Build with zero warnings; flash budget

## Phase 5: Web

- [x] T014 [US5] Types, `alertsSchema.ts` critical rule, `alertVariables.ts`, `alertsShared.ts` default texts and variable help, `defaults.ts`, mocks, `settingsDeps.ts`
- [x] T015 [US5] `AlertsEventsCard.tsx` roof rows (critical group with two levels and the note, outside the disabled fieldset), `AlertEventRow.tsx` level options; i18n `en.json` and `en.context.json`; Vitest; Playwright

## Phase 6: Docs

- [x] T016 `docs/user-guide/alerts.md` (roof events, levels, variables, critical rules), `close-on-unsafe.md` (alerts section), `configuration.md`, `docs/api/rest.md`, `docs/reference/settings-dependencies.md`, heritage, CHANGELOG; diagrams checked

## Phase 7: Verify

- [x] T017 Demo core rebuild; contract schemas and OpenAPI; spec 001 quickstart §1 (firmware, native, web typecheck/test/build, Playwright, quality gate, docs build, flash)
- [x] T018 `/speckit-converge` until converged; independent review (lost or suppressed critical alerts, alert storms) until it reports safe to merge
