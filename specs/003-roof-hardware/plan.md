# Implementation Plan: Roof Hardware

**Branch**: `spec/003-roof-hardware` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

Run spec 002's `Roof::Controller` on the board. A new pure library, `lib/RoofIoLogic`, holds every
decision the glue needs (reading scaling, switch debouncing, the watchdog rule, the stored fault
record, the park and weather gates, the settings-save guard, the wording and the status section),
each with native tests. The firmware adds `RoofControl` (the control task at 20 ms, the watchdog
timer, the inputs and the fault store), extends `MotorDriver` to drive the PWM and EN, wires the
park switches and rain contact into spec 004's `SafetyInputs`, adds the `roof` status section and
four authenticated REST commands, and refuses motion-settings saves while the roof moves. Docs: the
wiring page, a new calibration page, the REST and WebSocket references. Decisions are in
[research.md](research.md), types in [data-model.md](data-model.md), the API in
[contracts/roof-hardware.md](contracts/roof-hardware.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14 for the ESP32, native GCC/Clang for tests); TypeScript strict (web schema, demo mock) |
| **Primary Dependencies** | `lib/RoofLogic` (002), `lib/SafetyInputLogic` (004), `lib/DriverLogic`, `lib/ConfigModel`, ArduinoJson 6, FreeRTOS, esp_timer, LEDC, ADC oneshot (Arduino-ESP32 3.3.12), Unity, Zod, Vitest |
| **Storage** | Fault record: NVS `roofroof`/`roofFault`, 16 bytes. Settings: one new key in the `roof` section |
| **Testing** | `pio test -e native` (new `test_roof_io`, `test_roof_gates`, `test_roof_status`; config tests extended), Vitest (schema, ranges, contracts), bench checks |
| **Target Platform** | ESP32 DevKit (WROOM) |
| **Project Type** | Embedded firmware + pure library + web schema + docs |
| **Performance Goals** | A control step (13 ADC reads, the logic, the driver writes) well under 2 ms; a stop applied before the REST reply |
| **Constraints** | `lib/` pure (STRUCT-01); zero warnings; functions <= 60 lines and 5 parameters, complexity <= 15; files <= 600 lines; motion task stack headroom > 2 KB (VI); flash < 75% of the slot |
| **Scale/Scope** | ~500 lines of pure logic, ~600 lines of glue, ~700 lines of tests, one new setting, four routes |

## Constitution Check

| Principle | How this plan satisfies it |
|---|---|
| I. Motion Safety | EN/PWM low first in `setup()` (001, unchanged). Only `MotorDriver` writes 25/26/27, from `Driver::outputsFor`. A dedicated task at 20 ms; a 10 ms watchdog timer on the other core drops EN after 100 ms without a step and blocks EN until the fault is latched; the task watchdog restarts a hung task. A saved fault is latched before the first step; a damaged record latches too. Motion-settings saves refused while moving. Every decision is in `lib/RoofIoLogic` with tests. |
| II. Parked mount | `closeGate` before every close (park result parked AND fitted switches closed); `interlockLost` every step during a close latches `parkInterlock`. |
| III. Unsafe ends closed | Open refused unless the verdict is safe, and an open stops when it stops being safe. Faults and refusals state the value and the limit (`RoofText`). The rain contact feeds 004's verdict. The automatic close is 005. |
| IV. Alpaca | Untouched (006). |
| V. Pure logic | `lib/RoofIoLogic` has no Arduino headers; times and readings are parameters; every boundary tested. Glue verified on the bench (PR checklist). |
| VI. Budgets | One 4 KB task, one timer; stack headroom logged and shown (`stackFree.roof`). Status document capacity re-measured. Flash budget checked. Handlers on the async task only take the mutex (no network, no flash). |
| VII. UI | No new screen (009). The zod schema gains one roof field. |
| VIII. Security | All four roof routes call `requireAuth`; no secret is added. |
| IX. Docs | Wiring page updated, calibration page added, REST/WebSocket/configuration references, roof-motion user guide, heritage, DIA-11 re-confirmed against the new lib. |
| X. Quality | Quality gate with an empty baseline. |
| XI. Heritage | New files are RoofRoof-only, recorded in `heritage.md`. |

**Gate**: pass, no exceptions.

### Principle I-III rules and the tests that prove them

| Rule | Test |
|---|---|
| One module writes the pins; never both PWM | `test_driver_logic` (001); `MotorDriver::apply` orders writes (bench) |
| Missed deadline stops the motor | `test_roof_io`: `test_watchdog_trips_only_above_the_limit`, `test_watchdog_across_the_wrap`; bench SC-002 |
| A restart never clears the lockout | `test_roof_io`: `test_fault_record_round_trips_every_kind_and_cause`, `test_empty_store_is_no_fault`, `test_damaged_record_is_a_fault`; bench SC-003 |
| Unknown at boot between magnets, no motion | spec 002 tests; `RoofControl::begin` steps nothing before the task starts (bench) |
| Settings that change motion refused while moving | `test_roof_gates`: `test_settings_save_refused_only_while_moving`, `test_every_roof_and_park_field_counts_as_a_change` |
| II: close only onto a confirmed park | `test_roof_gates`: `test_close_needs_park_result_and_fitted_switches`, `test_unfitted_switches_are_ignored` |
| II: interlock re-checked throughout the close | `test_roof_gates`: `test_interlock_lost_only_while_closing`, `test_interlock_bits_name_the_input` |
| III: open refused while not safe | `test_roof_gates`: `test_open_needs_a_safe_verdict`, `test_open_stops_when_not_safe` |
| III: unreadable input is unsafe / not parked | `test_roof_io`: `test_switch_unreadable_until_confirmed`, `test_switch_chatter_is_unreadable` |
| III: texts state value and limit | `test_roof_status`: `test_every_fault_text_has_value_and_limit`, `test_refusal_texts` |

## Project Structure

### Documentation (this feature)

```text
specs/003-roof-hardware/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/roof-hardware.md
└── checklists/requirements.md
```

### Source code

```text
lib/RoofIoLogic/
├── include/  RoofReadings.h SwitchInput.h StepWatchdog.h FaultRecord.h RoofGates.h SettingsGuard.h RoofText.h RoofStatus.h
└── src/      RoofReadings.cpp SwitchInput.cpp FaultRecord.cpp RoofGates.cpp SettingsGuard.cpp RoofText.cpp RoofStatus.cpp
include/MotorDriver.h, src/MotorDriver.cpp        begin() (LEDC), apply(), trip(), tripped(), clearTrip(), enabled()
include/RoofControl.h, src/RoofControl.cpp        commands, settings, safety hand-over, snapshot
src/RoofControlTask.cpp                           the task step, inputs, watchdog timer
include/RoofFaultStore.h, src/RoofFaultStore.cpp  the NVS record
src/WebServerRoof.cpp                             /api/roof/* and the status section
src/main.cpp, src/WebServer*.cpp, include/WebServer.h   wiring, save guard, live update on change
lib/ConfigModel                                   roof.currentAmpsPerVolt
test/test_roof_io, test/test_roof_gates, test/test_roof_status (new); config tests extended
web/src/types/{config,status}.ts, validation/roofSchema.ts, mocks/{config,data}.ts, i18n
tools/api/{routes,operations}.json, docs/api/openapi.json, contracts schemas (status, config)
tools/i18n/gen_device_catalog.py (the new API source file)
docs/hardware/wiring.md, docs/hardware/calibration.md (new), docs/api/rest.md, websocket.md,
docs/user-guide/roof-motion.md, configuration.md, development/heritage.md, mkdocs.yml
platformio.ini (native include path)
```

**Structure Decision**: one new pure library beside `lib/RoofLogic` for the hardware layer's
decisions, so the firmware files only read pins, take locks and call it (STRUCT-01, constitution I's
"the hardware layer only applies what the logic decides").

## Phases

1. **Setup**: library skeleton, native include path, test suites.
2. **Foundational** (pure, test first): readings, switch inputs, watchdog, fault record, gates,
   settings guard, wording, status section; the new setting.
3. **US1/US2/US3** firmware: `MotorDriver`, `RoofFaultStore`, `RoofControl` and its task and
   watchdog, `main.cpp` wiring, SafetyInputs readings, REST commands, the save guard.
4. **US4** status section and live updates, web types and mocks, contract schemas.
5. **US5/US6** clear-fault route, docs.
6. **Polish**: OpenAPI, routes, i18n, demo core, quality gate, the full quickstart §1.

## Complexity Tracking

None.
