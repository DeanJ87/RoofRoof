# Implementation Plan: Wi-Fi and OTA Under Motion

**Branch**: `spec/010-network-under-motion` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/RoofIoLogic`: a new pure `PlatformHold` (which platform action waits for what: the roof
  moving, a close waiting, the close sequence busy; and the hotspot rule), and three extensions to
  `MotionGuards`: short flash writes in `FlashActivity`, a committed restart in `RestartPlan`, and
  both in `Blockers`/`blockerDecision`.
- `RoofControl::whileStill(action)`: runs a non-blocking `action` under the roof's lock only while
  the roof isn't moving and no close waits. Commands now probe their blockers under the same lock,
  so check and act are one step on both sides. The fault record's save counts as a short write.
- `PlatformGuard` (new, `src/`): the one place the firmware asks "may this platform action run
  now?" and registers it (short write, upload, committed restart), built on `whileStill`,
  `FlashGuard` and the close sequence's busy flag.
- Web server: every flash write, restart, upload and Wi-Fi change goes through `PlatformGuard`; the
  restart timer commits under the lock without waiting for it; roof open over the hotspot is
  refused.
- `MQTTClient::updateConfig` hands the settings to the main loop (spec 004 FR-011a).
- Command-line OTA: atomic start check, flash stays busy until the reset.
- `CloseOnUnsafe::busy()`; the reset reason in the start-up log.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the rule
table in [contracts/platform-hold.md](contracts/platform-hold.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5) |
| **Primary Dependencies** | spec 003's `RoofControl`, `FlashGuard`, `MotionGuards`; spec 005's `CloseOnUnsafe`; `MQTTClient`; `WebServer` |
| **Storage** | none new |
| **Testing** | `pio test -e native` (new `test_platform_hold`; extended `test_roof_gates`), deliberate breakage by hand (results in the PR), bench checks |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + docs |
| **Performance Goals** | a hold check is a few comparisons; `whileStill` holds the roof's lock for microseconds |
| **Constraints** | nothing new blocks the main loop, the web task or the timer task; flash growth under 8 KB |
| **Scale/Scope** | ~120 lines of lib logic, ~300 lines of tests, ~250 lines of glue |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Check and act share the roof's lock on both sides; every flash write counts as busy; restarts commit only at rest and then refuse moves; command-line OTA stays busy until the reset. Stop is never refused. | `test_platform_hold`, `test_roof_gates` (blockers) |
| II. Parked mount | Untouched; the close still goes through 003's gate and interlock. | existing |
| III. Unsafe ends closed | Restarts, firmware updates and Wi-Fi changes don't interrupt the close sequence. A committed restart refuses a close only for the moment before the reset. | `test_platform_hold` |
| IV. Alpaca | The hotspot rule is pure so spec 006 can apply it. | `test_platform_hold` |
| V. Pure logic | Every rule in `lib/RoofIoLogic`, tested at each case; the glue only asks and registers. Deliberate breakage of each rule. | as above |
| VI. Budgets | No new task, no blocking; the restart timer uses a non-blocking lock attempt. Flash measured. | build |
| VII. UI | No UI change (the refusals are 409s with text the UI already shows). | - |
| VIII. Security | No new endpoint; opening over the open hotspot refused. | `test_platform_hold` |
| IX. Docs | Roof-motion hold table, OTA, first setup, MQTT, REST and OpenAPI descriptions. | `mkdocs build --strict`, `diagrams.py` |
| X. Quality | check.py; functions ≤ 60 lines, complexity ≤ 15. | check.py |
| XI. Heritage | `MQTTClient` and `WebServer` diverge further from SQMeter (upstream candidate for the MQTT hand-over); recorded. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/010-network-under-motion/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/platform-hold.md
└── checklists/requirements.md

lib/RoofIoLogic/include/PlatformHold.h, src/PlatformHold.cpp   new: the hold rules, the hotspot rule
lib/RoofIoLogic/include/MotionGuards.h, src/MotionGuards.cpp   short writes, committed restart, blockers
include/PlatformGuard.h, src/PlatformGuard.cpp                 new: ask and register
include/FlashGuard.h, src/FlashGuard.cpp                       short writes
include/RoofControl.h, src/RoofControl.cpp                     whileStill, blockers under the lock, fault save
include/WebServer.h, src/WebServer.cpp, WebServerApi.cpp, WebServerBackup.cpp, WebServerUpdates.cpp,
  WebServerRoof.cpp, WebServerAlerts.cpp (saveArmedState only)  use PlatformGuard; hotspot rule
include/MQTTClient.h, src/MQTTClient.cpp                       settings hand-over
include/CloseOnUnsafe.h, src/CloseOnUnsafe.cpp                 busy()
src/main.cpp                                                    wiring, OTA, reset reason
test/test_platform_hold/                                        new
test/test_roof_gates/                                           extended
docs/user-guide/roof-motion.md, ota.md, mqtt.md, safety-inputs.md, docs/getting-started/first-setup.md,
  docs/api/rest.md, tools/api/operations.json, docs/api/openapi.json, docs/development/heritage.md, CHANGELOG.md
```

**Structure Decision**: the rules join `lib/RoofIoLogic`, where spec 003's motion guards already
live; the firmware side gets one small module so every caller asks the same question the same way.

## Phases

1. **Pure logic first (tests first)**: `test_platform_hold`, the `test_roof_gates` additions, then
   `PlatformHold` and the `MotionGuards` changes.
2. **Firmware**: `whileStill`, blockers under the lock, `FlashGuard` short writes, `PlatformGuard`,
   web server callers, restart timer, hotspot rule, MQTT hand-over, OTA, reset reason. Zero warnings;
   flash.
3. **Docs**: user pages, REST and OpenAPI, heritage, CHANGELOG.
4. **Verify**: deliberate breakage, quickstart §1 of spec 001, demo core, converge, independent
   safety reviews.

## Complexity Tracking

None.
