# Implementation Plan: Daylight Failsafe

**Branch**: `spec/013-daylight-failsafe` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/DaylightLogic` (new): SQMeter's sun position, the daylight state with its band, texts.
- `lib/CloseSequenceLogic`: `Inputs::daylight`, the daylight-only episode, the weather re-arm,
  daylight wording of the critical alerts, the daylight alert conditions, the restart flag.
- `lib/RoofIoLogic`: the daylight open refusal and stop, the status object, the Alpaca text.
- `lib/AlertLogic`: three event types and trackers, announced bits 4 and 5.
- `lib/ConfigModel`, `lib/DeviceCore`, `lib/SettingsDeps`: settings, rules, test samples,
  dependencies.
- Firmware: `CloseOnUnsafe` owns the `Daylight::Watch`; `main.cpp` hands daylight to the gates
  with the verdict; status writer fills `roof.daylight`.
- Demo core and panel: the same watch, "Sun up".
- Web: Roof card line and Open reason, Settings → Roof Daylight card, Settings → Alerts rows,
  schemas, mocks, i18n.
- Docs: a Daylight section in the close-on-unsafe guide, the commissioning guide and checklist (spec 012), configuration, alerts, REST, close-on-unsafe, settings dependencies,
  heritage, CHANGELOG.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the offered
interfaces in [contracts/](contracts/).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 005's `CloseSeq::Sequence` and `CloseOnUnsafe`; spec 003's gates and snapshot; spec 006's `checkCommand`; spec 007's alert engine; spec 009's Roof card and Roof tab; spec 011's demo core |
| **Storage** | NVS: two small sections and three event settings; RTC: one flag bit in the existing record |
| **Testing** | `pio test -e native` (new `test_daylight`, `test_close_daylight`, `test_close_daylight_properties`, `test_daylight_io`; `test_roof_alert_events` and the `test_settings_deps` fixtures extended; every spec 005 test unchanged); Vitest; Playwright |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | one sun evaluation a second on the main loop |
| **Constraints** | no blocking; flash within SIZE-04, growth over 15 KB recorded; UI size limits |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | No new motion path: the daylight close is the sequence's `Close` through spec 003's gates; position unknown and faults stand it down as before. | `test_close_daylight`: `test_daylight_*_cannot_move`, property test |
| II. Parked mount | The close waits for the confirmed park and passes the park gate; park not confirmed raises the critical alert. | `test_close_daylight`: `test_daylight_close_waits_for_park`, `test_daylight_park_timeout_alerts`, property "never closes unparked" |
| III. Unsafe ends closed | With daylight false the sequence is spec 005's; a daylight stand-down, halt or back-off re-arms when the weather turns unsafe; the verdict is never touched; nothing opens. | every `test_close_*` unchanged; `test_close_daylight`: `test_weather_rearms_*`, property "weather close starts within one step" |
| IV. Alpaca | `OpenShutter` refused with an Alpaca error naming daylight; ConformU unchanged (the simulator's daylight is off). | `test_dome_bridge`, ConformU CI |
| V. Pure logic | Daylight state, sequence changes, gates, alert trackers in `lib/` with boundary tests (band edges, 30 s and 600 s settles). | `test_daylight`, `test_roof_alert_events` |
| VI. Budgets | Main loop only; flash measured. | build, `flash_budget.py` |
| VII. UI | Existing rows and cards, i18n, a11y. | Vitest, Playwright, `size_check.py` |
| VIII. Security | No new endpoint; the settings save keeps `requireAuth`. | - |
| IX. Docs | Daylight section, commissioning stages, configuration, alerts, REST, close-on-unsafe; the close-sequence diagram re-checked. | `diagrams.py`, `mkdocs build --strict` |
| X. Quality | check.py, function limits. | check.py |
| XI. Heritage | `SunPosition` copied from SQMeter `877208b` unchanged into `lib/DaylightLogic`; `location.*` keeps SQMeter's names. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/013-daylight-failsafe/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
lib/DaylightLogic/ include/{Daylight.h,SunPosition.h} src/{Daylight.cpp,SunPosition.cpp}   (new)
lib/CloseSequenceLogic/ CloseSequence.h/.cpp, CloseSequenceText.cpp, RoofAlerts.h/.cpp
lib/RoofIoLogic/ RoofGates, RoofText, RoofStatus, DomeBridge
lib/AlertLogic/ AlertEngine.h/.cpp, RoofEvents.cpp, template-variables.json
lib/ConfigModel/, lib/DeviceCore/, lib/SettingsDeps/
include/CloseOnUnsafe.h, src/CloseOnUnsafe.cpp, include/RoofControl.h, src/RoofControl.cpp,
  src/RoofControlTask.cpp, src/main.cpp, src/WebServerStatus.cpp / WebServerRoof.cpp
tools/demo-core/emulated_roof.*, tools/alpaca-sim (openMustStop text)
test/test_daylight, test/test_close_daylight (new) + extended tests
web/src: types, validation, RoofCard, roofView, RoofTab (Daylight card), alerts rows, demo panel, mocks, i18n
docs/user-guide/close-on-unsafe.md (Daylight section), docs/hardware/commissioning*.md, configuration, alerts, close-on-unsafe, api/rest, reference, heritage, CHANGELOG
```

**Structure Decision**: one new pure library for the sun and the daylight state; the sequence
keeps one instance and gains one input, so there is still one path that closes the roof.

## Phases

1. **Pure logic, tests first**: `DaylightLogic`; sequence episode and re-arm; gates; alert
   trackers; config.
2. **Firmware glue**: watch, gates handover, status, alerts; zero warnings; flash.
3. **Demo core and web**: emulated roof, panel switch, Roof card, settings, alerts rows.
4. **Docs**.
5. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
