# Implementation Plan: Roof Alerts

**Branch**: `spec/007-roof-alerts` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/AlertLogic`: `AlertType::Roof` becomes seven typed events; `isCritical`; the roof trackers
  (fault, supply, source, closing, closed) in the engine with grace, settle, cooldown and the
  announced-before-reset bits.
- `lib/CloseSequenceLogic`: `RoofAlerts`: spec 005's critical decisions to typed alerts with their
  variables, and the roof conditions from the snapshot, verdict and phase.
- `lib/DeviceCore`: rules and settings for the new events, `finishCriticalAlert` (level floor, sound,
  wording with the reason kept, "Still: "), test samples, ntfy tags.
- `lib/ConfigModel`: seven event settings with defaults, the critical level rule.
- Firmware: `CloseOnUnsafe` sends typed critical alerts through the same sink and resend, and keeps
  the conditions; `WebServer` feeds them to the engine, keeps the announced bits in RTC memory and
  finishes critical alerts in `raiseRoofAlert`.
- Web: event rows, schema rule, variables, defaults, mocks, settings dependencies, i18n.
- Docs: alerts guide, close-on-unsafe, configuration, REST, settings dependencies, heritage,
  CHANGELOG.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the offered
interfaces in [contracts/](contracts/).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 005's `CloseSeq::Sequence`, `CloseOnUnsafe`, `AlertRetry`; the alert engine and dispatcher; spec 003's `RoofIo::Snapshot`; spec 004's `WeatherVerdict` |
| **Storage** | NVS: seven event settings in the main alerts part; RTC memory: one word of announced bits |
| **Testing** | `pio test -e native` (new: `test_roof_alerts`, `test_roof_alert_events`, `test_roof_alert_settings`; extended: `test_alert_clients`, `test_alert_variables`, `test_settings_deps` fixtures; unchanged and passing: every `test_close_*`); Vitest (new `RoofAlerts.test.tsx`: schema, rows, variables, settings deps); Playwright |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | conditions built every 100 ms (strings only while a condition holds); engine once a second |
| **Constraints** | no blocking on the main loop; flash within SIZE-04, growth over 15 KB recorded; UI size limits |
| **Scale/Scope** | ~300 lines of lib logic, ~700 lines of tests, ~80 lines of glue, ~200 lines of web |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Untouched: alerts only read the snapshot. A latched fault is reported, never cleared. | `test_roof_alerts`: conditions are read-only (no setter on the sequence or roof) |
| II. Parked mount | "Mount didn't park" can't be switched off (level floor in validation and at send) and bypasses pause, grace and master switch, as in spec 005. | `test_roof_alert_settings`: `test_critical_*`; every `test_close_*` unchanged |
| III. Unsafe ends closed | Critical alerts keep the resend and back-off; custom wording keeps the reason; every event's text carries the value and the limit or the input. Alerts never go through N.I.N.A. or SQMeter and never feed back into the verdict or the sequence. | `test_roof_alerts`: `test_critical_alert_*`, `test_conditions_*`; `test_alert_delivery` / `test_close_restart` unchanged |
| IV. Alpaca | Untouched. | - |
| V. Pure logic | Mapping, conditions, engine rules and finishing in `lib/`, with boundary tests (29/30/31 s settle, cooldown) and a randomised property test. | `test_roof_alert_events` |
| VI. Budgets | Main loop only; no new task; flash measured; RTC word only. | build, `flash_budget.py` |
| VII. UI | Existing `AlertEventRow` and `SettingsCard`, no new styles; every string through i18n; critical rows offer two levels and a note. | Vitest, Playwright, `size_check.py` |
| VIII. Security | No new endpoint; `/api/alerts/test` keeps `requireAuth`. | - |
| IX. Docs | alerts, close-on-unsafe, configuration, REST and settings-dependency pages updated; DIA-22 re-confirmed if its sources change. | `diagrams.py`, `mkdocs build --strict` |
| X. Quality | check.py; functions ≤ 60 lines, complexity ≤ 15. | check.py |
| XI. Heritage | `AlertEngine.h/.cpp` (copied from SQMeter) gain the roof types and trackers: recorded as diverged; the generic pieces (rising-edge tracker) marked as not an upstream candidate (SQMeter has no roof). | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/007-roof-alerts/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/roof-alerts.md
└── checklists/requirements.md

lib/AlertLogic/include/AlertEngine.h      types, isCritical, RoofInputs, rules, trackers
lib/AlertLogic/src/AlertEngine.cpp        names; update() calls updateRoof(); waits()
lib/AlertLogic/src/RoofEvents.cpp         (new) the roof trackers
lib/AlertLogic/template-variables.json    roof variables
lib/CloseSequenceLogic/include/RoofAlerts.h, src/RoofAlerts.cpp   (new) mapping and conditions
lib/RoofIoLogic/include/RoofText.h, src/RoofText.cpp              describeSupply public
lib/DeviceCore/src/DeviceAlerts.cpp, AlertDelivery.cpp, include/DeviceCore.h
lib/ConfigModel/ (Config.h, ConfigInternal.h, ConfigModel.cpp, ConfigValidate.cpp)
lib/SettingsDeps/ (catalogue.json, src/SettingsDeps.cpp)
include/CloseOnUnsafe.h, src/CloseOnUnsafe.cpp
include/WebServer.h, src/WebServerAlerts.cpp, src/main.cpp (one line)
test/test_roof_alerts, test/test_roof_alert_events, test/test_roof_alert_settings (new);
  test_alert_clients, test_alert_variables, test_settings_deps fixtures
web/src/types/alerts.ts, components/settings/{AlertsEventsCard,AlertEventRow,alertsShared,alertVariables,defaults}.ts(x),
  validation/alertsSchema.ts, lib/settingsDeps.ts, mocks, i18n/en.json, en.context.json, tests
docs/user-guide/alerts.md, close-on-unsafe.md, configuration.md, docs/api/rest.md,
  docs/reference/settings-dependencies.md, docs/development/heritage.md, CHANGELOG.md
```

**Structure Decision**: no new library; the roof trackers sit in the engine beside the imaging-app
ones (they share the grace, cooldown and stacking) but in their own source file, and the mapping
from the sequence sits with the sequence.

## Phases

1. **Pure logic first (tests first)**: engine roof trackers; mapping and conditions; finishing and
   rules; config.
2. **Firmware**: `CloseOnUnsafe`, `WebServer`, `main.cpp`; zero warnings; flash.
3. **Web**: types, schema, rows, variables, defaults, mocks, settings deps, i18n; Vitest and
   Playwright.
4. **Docs**: guides, references, heritage, CHANGELOG; diagrams.
5. **Verify**: demo core, contract schemas and OpenAPI, quickstart §1 of spec 001, converge,
   independent review (lost or suppressed critical alerts, alert storms) until safe to merge.

## Complexity Tracking

None.
