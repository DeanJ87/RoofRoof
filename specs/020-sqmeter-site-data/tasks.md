# Tasks: Site Data from SQMeter

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/sqmeter-site-api.md](contracts/sqmeter-site-api.md)

Tests come first for every `lib/` change (constitution V). `[P]`: can run in parallel.

## Phase 1: Setup

- [x] T001 Roadmap entry for 020 (stacked on 018) in `specs/ROADMAP.md`

## Phase 2: Foundational (pure logic, tests first)

- [x] T002 Native tests in `test/test_config_location/test_main.cpp`: `location.source` default `sqmeter`; JSON out (`source`, `sqmeter.gps`, `sqmeter.savedAt`); a stored section without `source` loads `manual` when `set` is true and `sqmeter` otherwise, with latitude and longitude unchanged; a stored document without `location` loads `sqmeter`; an unknown source is refused ("Location: source must be sqmeter or manual"); an API save keeping `sqmeter` keeps the device's `set`, `latitude`, `longitude`, `gps`, `savedAt`; an API save switching to `manual` takes the sent values; `location.sqmeter` never read from the API; the release fixtures in `test/fixtures` keep their location
- [x] T003 `LocationConfig::Source`, `gps`, `savedAt`; JSON in/out, migration and the keep rule in `lib/ConfigModel` (`Config.h`, `ConfigJsonIn.cpp`, `ConfigJsonOut.cpp`, `ConfigValidate.cpp`)
- [x] T004 Native tests in `test/test_sqmeter_site/test_main.cpp`: `siteUrl` (path replaced, port kept, IPv6 kept, https and junk refused, empty refused); `readLocation` (gps, manual, none, missing `sky`, out of range, NaN-like strings, non-200, unparseable); `LocationSchedule` (first at +15 s, generation makes it due at once, +10 min after good, +2 min after failed, never when busy, never in a safety-poll pass, disabled never); `locationToSave` (manual never, bad read never, nothing set saves, < 0.001° no save, ≥ 0.001° saves, GPS flag change saves, `savedAt` 0 without clock); `readSiteConfig` and `writeSiteDocument` (current SQMeter, v0.1 top-level timezone, v0.2.0-beta.1, missing parts null, masked and **unmasked** secrets only as `…Set` flags and their text absent from the output, wrong document is an error); `writeLocationStatus`
- [x] T005 `lib/SafetyInputLogic/include/SqmeterSite.h`, `src/SqmeterSite.cpp`

## Phase 3: User Stories 1-2 (firmware: location follows SQMeter)

- [x] T006 `SafetyPoller`: `fetchJson` (filter as read, total deadline), `pollLocation` in the loop under the schedule, `setBusy`, the read and its generation in the snapshot, the location target in `Targets` (`include/SafetyPoller.h`, `src/SafetyPoller.cpp`); `SafetyInputs::updateConfig` passes the location target
- [x] T007 `handleSite()` in `src/main.cpp`: busy flag to the poller every pass; once a second, `locationToSave` and the save through `WebServer::saveFromDevice` under `PlatformGuard::FlashWrite`
- [x] T008 The status's `location` section (`src/WebServerStatus.cpp`, `include/WebServer.h` site callback)

## Phase 4: User Story 3 (firmware: copy)

- [x] T009 `POST /api/sqmeter/site` in `src/WebServerSite.cpp` (auth, paused request, `site_job` task, one at a time, refused while busy, 400/409/502/503)
- [x] T010 Firmware builds with zero warnings (`esp32dev`, `esp32dev_bench`); flash budget

## Phase 5: User Story 5 (demo)

- [x] T011 Simulated SQMeter in `tools/demo-core` (a GPS fix and a settings document through the same lib functions; the status's `location`; `sqmeterOffline` injector that also fails the weather source; the copy route in the bridge); WASM rebuilt
- [x] T012 Demo handlers (`web/src/demo/handlers.ts`, `device.ts`, `coreApi.ts`), the panel injector, MSW mocks (`web/src/mocks`)

## Phase 6: User Stories 1, 3, 4 (web)

- [x] T013 Types, zod schema, defaults and the shared range fixture for `location.source` (`web/src/types/config.ts`, `status.ts`, `validation/configSchema.ts`, `test/fixtures/config-ranges.json`); Vitest
- [x] T014 `web/src/lib/sqmeterCopy.ts`: fetch, rows from the site document against the config, `changesOf(rows, ticked)` with the secret clearing; Vitest
- [x] T015 `SqmeterCopy` component (`web/src/components/settings/SqmeterCopy.tsx`) and `LocationFields` with the source select and the kept value (`RoofTab.tsx`); Vitest
- [x] T016 Wizard: the `sqmeter` screen first on the Site and time page (`wizard/screens.ts`, `DevicePages.tsx`), the Site screen with the source; texts `wizard.q.sqmeter`, `wizard.why.sqmeter`; Vitest
- [x] T017 Dashboard daylight row: the location's source, age and problem (`web/src/dashboard/RoofCard.tsx`); Vitest
- [x] T018 i18n texts and context notes (`web/src/i18n/en.json`, `en.context.json`); device text catalogue if device texts change
- [x] T019 Playwright: Settings copy and the wizard's SQMeter screen in the demo; axe; a11y inventory; screenshots; UI size

## Phase 7: Polish

- [x] T020 Settings-deps catalogue: `location.source` sqmeter needs `safety.http.url` (`lib/SettingsDeps/catalogue.json`, generated code, `tools/settings-deps/check.py`)
- [x] T021 Route registry and OpenAPI: `tools/api/routes.json`, `operations.json`, `docs/api/openapi.json`, contract schemas (`tools/contracts/generate_schemas.py`)
- [x] T022 Docs: the daylight section (`docs/user-guide/close-on-unsafe.md`), `configuration.md`, `setup-wizard.md`, `dashboard.md`, `docs/api/rest.md`, `websocket.md`; `docs/development/heritage.md`; `CHANGELOG.md`
- [x] T023 Quality gate, `tools/dashboard/check.py`, i18n checks, docs build, flash budget, demo-core source hash
