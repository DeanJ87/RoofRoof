# Implementation Plan: Test Box Bypass

**Branch**: `spec/022-test-box-bypass` | **Date**: 2026-11-23 | **Spec**: [spec.md](spec.md)

## Summary

Three owner-set, expiring bypasses for a box with no telescope and no weather station (park,
weather, daylight close), applied once where the park result, the weather verdict and the daylight
view are made, so every gate, the interlock and the automatic close see them the same way. Faults,
the watchdog and the supply check are untouched. The bypass is shown on every surface (banner,
status, MQTT, Home Assistant, Alpaca, alerts, log) and set from Settings or the wizard.
Constitution 3.0.0 amends Principles II and III to allow exactly this.

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 004's verdict and park rule, spec 021's close hold, spec 013's daylight watch, spec 007's alert engine, spec 008's status/MQTT/HA, spec 006's Dome, spec 018's wizard |
| **Storage** | the `testBox` settings group (JSON in NVS, with the rest of the settings) |
| **Testing** | `pio test -e native` (new `test_test_box` and `test_test_box_surfaces`; extended `test_ha_discovery`, `test_config_backup`, `test_settings_deps`, `test_device_core`, `test_roof_mqtt`, `test_roof_status`); Vitest (banner, Test box card, pills, wizard screen, schema); Playwright (demo: turn on, banner, open accepted) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | O(1) per evaluation; no flash writes outside a save |
| **Constraints** | flash near its warning line: small additions only |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | No fault, timeout, watchdog, supply or drive-budget change; the bypass only feeds the park and weather inputs the gates already use | `test_test_box` (every fault latches with every bypass on), the existing roof suites unchanged |
| II. Parked mount | **Amended (3.0.0, MAJOR)**: "no telescope" stands in for N.I.N.A. only as an explicit, visible, owner-set, expiring test-box setting, never with a N.I.N.A. address; switches still count; one park result for every path | `test_test_box` |
| III. Unsafe ends closed | **Amended (3.0.0, MAJOR)**: with no weather source configured, "no weather source" may be bypassed by the same setting; readings and configured sources' doubt still count; the UI/MQTT/alerts only show it | `test_test_box` |
| IV. Alpaca | The description changes; behaviour and conformance unchanged | `test_alpaca_description`, ConformU in CI |
| V. Pure logic | `TestBox` in `lib/SafetyInputLogic`, the stamp in `lib/ConfigModel`, daylight and alerts in their libs; tests first | native suites |
| VI. Budgets | a few hundred bytes of code; flash measured | build, `flash_budget.py`, `size_check.py` |
| VII. UI | existing Note (bad tone), Toggle, NumberInput, ChoiceGroup; the reason shows on the park and weather rows | Vitest, Playwright |
| VIII. Security | `POST /api/config` (requireAuth) is the only setter; `until` never from a body | `test_test_box` |
| IX. Docs | new user guide page; configuration, REST, MQTT, Alpaca, safety inputs, daylight, alerts, wizard pages; OpenAPI and contracts | `mkdocs build --strict` |
| X. Quality | check.py, function limits | check.py |
| XI. Heritage | no copied module changes behaviour beyond RoofRoof's own sections | - |

**Gate**: pass with the MAJOR amendment (governance: Principles I-III are relaxed only by a MAJOR
amendment that states the new risk; spec, constitution Sync Impact Report).

## Project Structure

```text
specs/022-test-box-bypass/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
.specify/memory/constitution.md (3.0.0), specs/ROADMAP.md, CHANGELOG.md
lib/ConfigModel/ Config.h (TestBoxConfig, AlertsConfig::testBox), ConfigJsonIn/Out, ConfigValidate, ConfigModel, ConfigInternal, TestBoxSave.cpp (new)
lib/SafetyInputLogic/ TestBox.h, TestBox.cpp (new); ParkRule.h, SafetyInput.h (bypassed flags and texts)
lib/DaylightLogic/ Daylight.h/.cpp (testBox, bypassed); lib/ConfigBackup (a restore brings no deadline)
lib/RoofIoLogic/ RoofStatus.h/.cpp (TestBoxStatus)
lib/AlertLogic/ AlertEngine.h, RoofEvents.cpp (TestBox event); lib/DeviceCore/ DeviceAlerts.cpp, AlertDelivery.cpp
lib/AlpacaLogic/ AlpacaRouter.h, AlpacaDome.cpp (description); lib/HaDiscovery (test_box sensor)
lib/SettingsDeps (entries for testBox)
include/, src/: SafetyInputs, CloseOnUnsafe, WebServerApi (stamp), WebServerStatus, WebServerAlpaca, main (boot log)
tools/demo-core: emulated_roof (apply), emulated_device (stamp, status, description)
test/test_test_box (new) and the extended suites
web/src: types, validation, settings/TestBoxCard.tsx and TestBoxBanner.tsx (new, beside the bench banner), roofView pills, wizard screen, alerts, i18n, mocks
docs: user-guide/test-box.md (new) and the pages above; docs/api/openapi.json; contract schemas
```

## Phases

1. Constitution, roadmap, spec docs.
2. Pure logic, tests first: settings, stamp, `TestBox`, daylight, status, alert, Dome, HA, deps.
3. Firmware glue and demo core.
4. Web UI and tests.
5. Docs, contracts, OpenAPI.
6. Verify: quickstart §1 of spec 001, converge, review rounds until safe to merge.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| Principles II and III relaxed for a test box | The owner asked for a way to test a box with no telescope and no station; without it nothing that moves can be tried on the desk | Pointing the owner at the bench build: it has no motor, so the real motor, Hall sensor and driver can't be tested |

The relaxation is made by the MAJOR amendment itself (governance), not by an exception to it.
