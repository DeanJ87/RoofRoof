# Implementation Plan: Site Data from SQMeter

**Branch**: `spec/020-sqmeter-site-data` | **Date**: 2026-11-19 | **Spec**: [spec.md](spec.md)

Stacked on spec 018 (PR #20, the wizard redesign); rebased once it merges.

## Summary

- `lib/ConfigModel`: `LocationConfig` gains `source` (`Sqmeter`/`Manual`), `gps` and `savedAt`;
  JSON in and out (`location.source`, `location.sqmeter.{gps,savedAt}`); the migration rule (a
  section without `source` is `manual` when `set`, else `sqmeter`; a new device is `sqmeter`); the
  API save keeps the device's location while the source stays `sqmeter`.
- `lib/SafetyInputLogic` gains `SqmeterSite.h` (pure, native-tested):
  - `Site::siteUrl(safetyUrl, path)`: SQMeter's address from the weather source's;
  - `Site::statusFilter`, `Site::readLocation`: the `sky` section of `GET /api/status` into a
    `LocationRead` (good, or a problem);
  - `Site::LocationSchedule`: when a read is due (10 min / 2 min / soon after a change), never in
    a pass with the safety poll, never while busy;
  - `Site::locationToSave`: the save rule (0.001°, the GPS flag, nothing set);
  - `Site::configFilter`, `Site::readSiteConfig`, `Site::writeSiteDocument`: SQMeter's settings
    into the site document, secrets reduced to `…Set` flags;
  - `Site::writeLocationStatus`: the status's `location` section.
- Firmware:
  - `SafetyPoller`: a location read in its loop (`pollLocation`) through a bounded,
    filter-as-you-read JSON fetch (`fetchJson`, also used by the copy); a `busy` flag the main
    loop sets; the location read in its snapshot with its generation.
  - `main.cpp` `handleSite()`: once a second, saves a read that `locationToSave` accepts through
    `WebServer::saveFromDevice` under the flash-write guard; tells the poller whether the roof is
    busy.
  - `WebServerSite.cpp`: `POST /api/sqmeter/site` (auth, a paused request answered from a
    short-lived job task, one at a time, refused while busy); the status's `location` section.
- Demo core: a simulated SQMeter (a GPS fix and a settings document) read through the same lib
  functions; an `sqmeterOffline` injector that also stops the weather source answering; the copy
  route.
- Web: `location.source` in types, zod schema and defaults; `LocationFields` with the source select
  and the kept value; a `SqmeterCopy` component (fetch, review list, apply) in Settings → Roof and
  on the wizard's new `sqmeter` screen (first on the Site and time page); the dashboard's daylight
  row; MSW mocks; i18n; Vitest; Playwright.
- Registries and docs: routes, operations, OpenAPI, contract schemas, settings-deps catalogue
  (`location.source` depends on `safety.http.url`), config ranges fixture, user guide, REST and
  WebSocket references, configuration reference, heritage note, CHANGELOG, ROADMAP entry.

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | ArduinoJson 6 (`DeserializationOption::Filter`), HTTPClient + `DualStackClient` (spec 004's poller), `Net::parseHttpUrl`, spec 010's `PlatformGuard::FlashWrite`, spec 013's daylight, spec 018's wizard |
| **Storage** | the existing settings document (NVS); three new location fields, about 60 bytes |
| **Testing** | `pio test -e native` (new `test_sqmeter_site`, `test_config_location`; `test_config_migration` extended); Vitest; Playwright (`settings`, `wizard`) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | a location read ≤ 3 s once per 10 min; the copy ≤ 5 s on its own task; JSON parsed through a filter as it arrives (no whole-body buffer) |
| **Constraints** | flash at 83.6 % before this spec: keep additions lean; no blocking on the async task; never delay motion or safety work |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Nothing here touches the motion task. The location read runs on the poller; the copy on its own job task; neither is waited for by a move. The location save is a flash write under spec 010's guard and waits while the roof moves or a close waits | `test_sqmeter_site` (schedule never busy), bench |
| II. Parked mount | Untouched: the park check never sees SQMeter's location | — |
| III. Unsafe ends closed | The location never enters the verdict; the safety poll keeps its schedule (the read never shares its pass). Daylight with no location behaves as before | `test_sqmeter_site`, `test_daylight` unchanged |
| IV. Alpaca | Untouched | ConformU unchanged |
| V. Pure logic | Parsing, schedule, save rule, migration, site document and status section in `lib/`, tests first | native suites |
| VI. Budgets | Filtered parsing into small documents; no new long-lived task (the job task lives ≤ 5 s); request handler never blocks; flash measured | build, `flash_budget.py`, `size_check.py` |
| VII. UI | Settings controls and the onboarding set only; no new styles; i18n | Vitest, Playwright + axe |
| VIII. Security | The copy route calls `requireAuth`; the site document never carries a secret's value; a copied broker or ntfy server clears the matching saved secret | `test_sqmeter_site` (unmasked old SQMeter), Vitest |
| IX. Docs | User guide, references, configuration, OpenAPI regenerated | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | Functions ≤ 60 lines and 5 parameters, complexity ≤ 15 | `tools/quality/check.py` |
| XI. Heritage | Field names follow SQMeter's (`location.latitude`, `sky.locationSource`); noted in heritage.md | — |

No exceptions; Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/020-sqmeter-site-data/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/sqmeter-site-api.md
├── checklists/requirements.md
└── tasks.md
```

### Source Code (repository root)

```text
lib/ConfigModel/                     # LocationConfig, JSON in/out, migration, validation
lib/SafetyInputLogic/include/SqmeterSite.h
lib/SafetyInputLogic/src/SqmeterSite.cpp
include/SafetyPoller.h, src/SafetyPoller.cpp   # pollLocation, fetchJson, busy flag
include/WebServer.h, src/WebServerSite.cpp     # POST /api/sqmeter/site, saveFromDevice
src/WebServerStatus.cpp              # the status's location section
src/main.cpp                         # handleSite()
tools/demo-core/                     # simulated SQMeter, the route, the injector
test/test_sqmeter_site/, test/test_config_location/
web/src/types/, web/src/validation/configSchema.ts, web/src/mocks/
web/src/components/settings/RoofTab.tsx, settings/SqmeterCopy.tsx, lib/sqmeterCopy.ts
web/src/components/wizard/{screens.ts,DevicePages.tsx}
web/src/dashboard/RoofCard.tsx
web/src/demo/{handlers.ts,device.ts}
tools/api/{routes.json,operations.json}, docs/api/openapi.json, tools/contracts/
lib/SettingsDeps/catalogue.json, test/fixtures/config-ranges.json
docs/user-guide/{close-on-unsafe.md,configuration.md,setup-wizard.md,dashboard.md}, docs/api/{rest.md,websocket.md}
```

**Structure Decision**: the site logic joins `lib/SafetyInputLogic`, which already parses SQMeter's
safety document (`SqmeterSource.h`), rather than a new library with its own include paths.

## Complexity Tracking

None.
