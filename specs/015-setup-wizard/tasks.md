# Tasks: Setup Wizard

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).
Web logic in Vitest; pages in Playwright.

**Organisation**: the pure logic first (settings, report), then firmware, demo, web and docs.

## Phase 1: Setup

- [ ] T001 Add "015" to `specs/ROADMAP.md`

## Phase 2: Tests first

- [ ] T002 [US1] [US4] `test/test_config_setup`: defaults `new`/0; round trip of each state; an unknown state and a step out of 0-9 refused; a partial save without `setup` keeps it; `storedBeforeSetup` true for a document without `setup` and false with one; `markStoredBeforeSetup` gives `offered`/0; `setup` in `toJson`
- [ ] T003 [US4] `test/test_config_migration`: every release fixture loads with `setup.state` `offered` after the load migration, every other setting unchanged (SC-004)
- [ ] T004 [US2] `test/test_inputs_report`: each verdict, each source state name, rain contact names, park text, each N.I.N.A. state, connected/atPark/slewing, ages (N.I.N.A. from `takenAtMs`, saturating), generations, texts equal to `describe`/`describeSource`; no `http://` or topic in the output; fits `INPUTS_REPORT_CAPACITY` with long failure texts

## Phase 3: Pure logic

- [ ] T005 `lib/ConfigModel` `SetupConfig` until T002-T003 pass
- [ ] T006 `lib/SafetyInputLogic` `InputsReport` until T004 passes

## Phase 4: Firmware (US1, US2, US4)

- [ ] T007 `src/ConfigStore.cpp`: `Config::load` calls `markStoredBeforeSetup` when the stored document has no `setup`
- [ ] T008 `SafetyInputs`: the report under its own mutex, refreshed in `evaluate`; MQTT generation bumped with `change.sqmeterMqtt`; `report()` for any task
- [ ] T009 `WebServer`: `setInputs`; `GET /api/roof/inputs` (copies the report, writes it, 200); `main.cpp` wiring; `pio run -e esp32dev` with zero warnings

## Phase 5: Demo core (US2)

- [ ] T010 `tools/demo-core`: the emulated roof keeps the N.I.N.A. reading and the three generations; `inputsDocument()` through `writeInputsReport`; bridge export; `bash tools/demo-core/build.sh`

## Phase 6: Web (US1-US5)

- [ ] T011 [P] Types (`SetupConfig`, `RoofInputs`), zod schema for `setup`, mocks (config without `setup`, inputs document), demo handler for `/api/roof/inputs`, demo defaults `setup.state` `done`
- [ ] T012 `useConfigEditor` taken out of `Settings.tsx` (load, normalise, validate, dirty, save); Settings unchanged in behaviour (its tests pass)
- [ ] T013 Exports from the settings tabs for reuse: password fields, time zone and NTP cards, location fields, daylight mode fields, Hall card, current card, park switches field, weather sources (without rain and stale time)
- [ ] T014 [US1] [US5] `/wizard`: page shell (page list nav, heading focus, Back, Skip page, Save and continue, Skip setup with its warning), the motion note, the ten pages (FR-008), the live readings (WebSocket status), no roof command anywhere
- [ ] T015 [US2] Test panel and `inputsTest.ts` (FR-009) with Vitest: changed part waits for a new generation and a non-waiting state; unchanged part shows the current reading; 15 s limit; a refused save ends the test with the reason
- [ ] T016 [US1] Review page and `review.ts` (FR-010) with Vitest for every check's states; Finish (done, step 0, restart note); links to commissioning and its unattended gate (FR-011)
- [ ] T017 [US3] [US4] `SetupPrompt` in the layout (redirect once per load for `new`, not in hotspot mode; banner for `new`/`offered`; Dismiss); System page Setup card; demo panel "Restart setup wizard"; Vitest
- [ ] T018 i18n keys with context notes; `node tools/i18n/check.mjs`, `literals.mjs`; `tools/ui/copy_check.py`, `label_check.py`
- [ ] T019 Playwright `tests/wizard.spec.ts`: walk all pages in the demo saving each; Test on weather and N.I.N.A.; a save refused while the roof moves with the Stop bar shown; no Open/Close/Clear fault on any page; axe on every page at phone width; resume after reload; skip; run again from System; screenshots in `screenshots.spec.ts`; dashboard inventory entry for the banner

## Phase 7: Contracts and API

- [ ] T020 `tools/api/routes.json`, `operations.json`, example; contract sample `device-roof_inputs` and the config samples with `setup`; `generate_schemas.py` (new document); `python3 tools/api/openapi.py`

## Phase 8: Docs

- [ ] T021 `docs/user-guide/setup-wizard.md` (new, in `mkdocs.yml`); first boot's "Next" step; commissioning (the wizard covers stages 2-5's settings, not the tests); configuration (`setup.*`); REST (`/api/roof/inputs`); dashboard (the banner); live demo (restart); CHANGELOG

## Phase 9: Verify

- [ ] T022 Quickstart §1 of spec 001 (build, native, web typecheck/test/build, Playwright, quality gate, flash budget, docs build); `tools/settings-deps/check.py`, `tools/dashboard/check.py`; UI size baseline
