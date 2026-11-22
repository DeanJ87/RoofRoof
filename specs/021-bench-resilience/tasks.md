# Tasks: Bench Resilience

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/config-save.md](contracts/config-save.md)

## Phase 1: Setup

- [ ] T001 Roadmap entry for 021 in specs/ROADMAP.md (scope, base, out of scope)

## Phase 2: Park ride-through (US1, US2, US3) - pure logic first

- [ ] T002 [US1] Native tests `test/test_park_grace/test_main.cpp`: memory (good replaces, failure keeps, Waiting/NotConfigured/new generation forget); `closeHold` for parked, each failure kind, 10 000 vs 10 001 ms, failure then not at park / slewing / not connected, failure with a switch open or unreadable, disagreement, remembered reading not parked; a timeline of evaluations and control steps
- [ ] T003 [US1] `NinaMemory`, `rememberNina`, `CloseHold`, `closeHold`, `describe(CloseHold)` in lib/SafetyInputLogic/include/ParkRule.h and src/ParkHold.cpp
- [ ] T004 [US2] Tests in test/test_roof_gates: `closeMayContinue` and `interlockLost` with the ride-through, the bound at `nowMs == until` and `+1`, a live switch opening, a stale hand-over clearing the ride-through, the start gate refusing on a ride-through, `interlockBits` with `InterlockNinaStale`
- [ ] T005 [US2] `GateInputs` fields, `closeMayContinue`, `interlockLost`, `interlockBits`, `currentGates` in lib/RoofIoLogic; the fault text in src/RoofText.cpp
- [ ] T006 [US2] The calibration run's open-leg backstop uses `closeMayContinue` (lib/CalibrationLogic/src/CalibrationRunLegs.cpp); test in test/test_calibration_run
- [ ] T007 [US1] Firmware: SafetyInputs keeps the memory and hold and logs a ride-through; main loop hands the hold over; RoofControl::setSafety stores it and currentGates stamps `nowMs`
- [ ] T008 Demo core rebuilt (`tools/demo-core/build.sh`)

## Phase 3: Stale saves (US4, US5) - pure logic first

- [ ] T009 [US4] Native tests `test/test_config_save/test_main.cpp`: no revision; same revision; other revision with equal values (numbers by value, null vs missing, masked secret); conflicts named in dotted form; arrays; no base values; the 16-name cap; the body's settings unaffected by the two keys
- [ ] T010 [US4] `ConfigSave::readBase`, `staleFields`, `checkSave` in lib/ConfigModel
- [ ] T011 [US4] Firmware: `UpdateConfigCallback` in src/main.cpp (check, merge and write under the lock, revision bumped inside it), WebServer wiring, the save handler's 409 and revision, `X-Config-Revision` on GET, random seed
- [ ] T012 [US4] Web: `web/src/lib/configPatch.ts` (diff, credential partners, base values, rebase) with Vitest
- [ ] T013 [US4] Web: `settingsApi.saveConfig` with a base, `fetchConfig` with the revision; `useConfigEditor` keeps the raw document and revision, adds the `conflict` outcome and `rebase`; Vitest with two editors against one mock device
- [ ] T014 [US4] Web: Settings toast with Reload; wizard and calibration page reload at once and say so; i18n and context notes
- [ ] T015 [US5] Vitest: a sync-style change to another field doesn't block a save; the same field does
- [ ] T016 Mocks and demo: the mock device's GET header and revision; demo unchanged (no revision, legacy path)

## Phase 4: Docs and contracts

- [ ] T017 Constitution 2.1.1 (Principle II clarification) with its Sync Impact Report
- [ ] T018 OpenAPI: `tools/api/operations.json` (header, base keys, 409 body), `docs/api/openapi.json` regenerated; docs/api/rest.md
- [ ] T019 User docs: docs/user-guide/safety-inputs.md and close-on-unsafe.md (ride-through), configuration.md (two pages, Reload); heritage note for the save handler; CHANGELOG.md

## Phase 5: Verify

- [ ] T020 Quickstart §1 of spec 001: firmware build, native tests, web typecheck/test/build, Playwright, quality gate, flash and UI budgets, docs build, source hash
