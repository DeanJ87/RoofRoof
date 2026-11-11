# Implementation Plan: Roof State and Motion Logic

**Branch**: `spec/002-roof-motion-logic` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

Add `lib/RoofLogic`, a pure C++ library that decides the roof's state, the driver command and duty
for each control step, every motion fault and the fault latch, from plain inputs (time, Hall counts,
supply volts, motor amps). Add the roof settings to the settings model (device, web schema, shared
range fixture, settings-dependency catalogue) so spec 003 can build the logic's settings from the
config. Prove every Principle I rule with native tests, including seeded random-sequence (property)
tests against a simulated roof. No firmware code calls the logic yet (spec 003). Decisions are in
[research.md](research.md); the types are in [data-model.md](data-model.md) and the API in
[contracts/roof-logic.md](contracts/roof-logic.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (native GCC/Clang for tests, GCC 14 for the ESP32); TypeScript strict (web schema) |
| **Primary Dependencies** | `lib/DriverLogic` (driver command), ArduinoJson 6 (settings model only), Unity, Zod, Vitest |
| **Storage** | The roof settings are a new `roof` section of the stored config JSON (NVS key `config`, limit 5100 bytes) |
| **Testing** | `pio test -e native` (new `test/test_roof_logic`, `test/test_roof_properties`, settings tests), Vitest (schema, ranges) |
| **Target Platform** | ESP32 DevKit (linked from spec 003 on); native desktop for tests |
| **Project Type** | Embedded firmware library + settings model + web schema + docs |
| **Performance Goals** | One control step in well under 1 ms on the ESP32 (no loops longer than the ramp-up integration, at most 3000 iterations, done once per move start) |
| **Constraints** | `lib/` pure (STRUCT-01); no heap allocation per step; functions <= 60 lines, <= 5 parameters, complexity <= 15; files <= 600 lines; zero warnings; computed times fit 32 bits for every valid setting |
| **Scale/Scope** | ~800 lines of logic, ~1500 lines of tests, 15 new settings |

## Constitution Check

| Principle | How this plan satisfies it |
|---|---|
| I. Motion Safety | Every rule of Principle I that is a decision lives in `lib/RoofLogic` with tests (table below). The logic's output goes only through `Driver::outputsFor`, so the never-both rule stays in one function. The dead time is enforced by the logic. The fault latch survives a restart through `restoreFault` (spec 003 saves it). |
| II. Parked mount | Not decided here. `raiseFault` lets spec 005 stop a close as a motion fault when the park interlock drops (Principle II's last rule). |
| III. Unsafe ends closed | Not decided here. Refusals and faults carry the measured value and the limit, so 003/005/007 can state them. Automatic moves are refused from an unknown position, so 005's close can't move a roof it can't place. |
| IV. Alpaca | Untouched. The states map one-to-one onto ShutterStatus for spec 006 (research R9). |
| V. Pure logic | `lib/RoofLogic` has no Arduino headers; time and readings are parameters; every transition and boundary has a native test, plus 10,000-sequence property tests. |
| VI. Budgets | The library isn't linked into the firmware until 003; the settings model grows by one section (the stored JSON stays far below 5100 bytes, measured by a test). Flash budget re-checked. |
| VII. UI | No UI. The zod schema gains the `roof` section so a save from the UI keeps the device's rules; there is no new screen. |
| VIII. Security | No new route. |
| IX. Docs | New user page `docs/user-guide/roof-motion.md` with the state machine as a blocking safety diagram (DIA-21) and the settings; configuration reference, REST config example and heritage updated. |
| X. Quality | `tools/quality/check.py` passes with an empty baseline. |
| XI. Heritage | `lib/RoofLogic` is RoofRoof-only; recorded in `heritage.md`. |

**Gate**: pass, no exceptions.

### Principle I rules and the tests that prove them

| Rule | Test (`test_roof_logic`, `test_roof_faults`, `test_roof_profile`; property tests in `test_roof_properties`) |
|---|---|
| Never both directions; direction change via stop and dead time | properties P1 (`test_property_every_sequence_keeps_the_safety_rules`); `test_reversal_waits_the_dead_time_off`, `test_a_restart_after_stop_waits_the_dead_time`; `test_driver_logic` (pin levels) |
| Never move by itself from unknown | `test_boot_between_magnets_is_unknown_and_off`, `test_automatic_moves_refused_when_unknown`, `test_person_move_from_unknown_is_slow_and_stops_at_the_first_magnet`, `test_abort_part_way_makes_the_position_unknown`; property P4 |
| Hard timeout, from settings, enforced by the logic | `test_fault_timeout_at_the_limit_not_before`, `test_fault_timeout_of_a_move_from_unknown`, `test_fault_did_not_leave_the_start_magnet`, `test_timeouts_add_the_margin`; property P5 |
| Stops on its target magnet | `test_open_stops_on_the_open_magnet`, `test_close_stops_on_the_closed_magnet`; property P6 |
| Any fault stops, drops EN, locks out; a restart doesn't clear it | each `test_fault_*`, `test_a_fault_refuses_every_move_until_a_person_clears_it`, `test_a_restored_fault_latches_before_the_first_step`, `test_raise_fault_stops_at_once`; property P3 |
| Fault kinds: timeout, over-current, wrong magnet, Hall at a rail, supply low, missed loop deadline | `test_fault_timeout_*`, `test_fault_over_current_after_the_confirm_time`, `test_fault_wrong_magnet_*`, `test_fault_hall_rail_*`, `test_fault_supply_low_during_a_move`, `test_fault_late_step_while_driving` |
| Ramp never exceeds the configured duty | `test_ramp_up_is_linear_from_slow_to_top`, `test_profile_never_outside_slow_and_top`, `test_slows_down_near_the_end`; property P2 |
| Settings that change motion refused while moving | `test_settings_refused_while_moving` |

## Project Structure

### Documentation (this feature)

```text
specs/002-roof-motion-logic/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md
├── contracts/roof-logic.md      the library's API, for specs 003, 005, 006, 011
├── checklists/requirements.md
└── tasks.md
```

### Source code

```text
lib/RoofLogic/
├── include/
│   ├── RoofTypes.h          enums (State, Command, Origin, End, HallZone, FaultKind, RefusalReason), Fault, Refusal, Readings, Output, names
│   ├── RoofSettings.h       Roof::Settings + validSettings()
│   ├── HallFilter.h         Hall classification with hysteresis and confirmation
│   ├── MotionProfile.h      duty for a step, estimate advance, expected move time, timeouts
│   └── RoofController.h     the state machine: command(), tick(), clearFault(), raiseFault(), restoreFault(), applySettings(), status(), output()
└── src/
    ├── RoofTypes.cpp        machine names
    ├── RoofSettings.cpp
    ├── HallFilter.cpp
    ├── MotionProfile.cpp
    ├── RoofController.cpp   commands and fault latch
    └── RoofControllerStep.cpp  the per-step move logic (keeps each file well under 600 lines)
lib/ConfigModel/             RoofConfig in Config.h; defaults, JSON in/out, validation
test/test_roof_profile/      settings, Hall filter, profile and timeouts
test/test_roof_logic/        moves: open/close, unknown position, stop, abort, reversal, settings
test/test_roof_faults/       every fault kind at its boundary, the latch and clearing
test/test_roof_timing/       dead time by steps and after start-up, drive budget, abort at rest, reading sanity
test/test_roof_properties/   seeded random sequences against the simulator
test/roof_sim/RoofSim.h      the simulated roof shared by those suites
test/test_config_model/, test/test_config_migration/, test/test_config_backup/  roof section
test/fixtures/config-ranges.json, test/fixtures/settings-deps/default-config.json
lib/SettingsDeps/catalogue.json   roof.* in noDependency (each roof setting depends only on its own section)
web/src/types/config.ts, web/src/validation/roofSchema.ts, configSchema.ts, mocks/config.ts, i18n/en.json (+ context)
specs/001-platform-bootstrap/contracts/schemas/config.schema.json (regenerated: the roof section)
docs/user-guide/roof-motion.md (new), configuration.md, api/rest.md, development/heritage.md, mkdocs.yml
platformio.ini               -Ilib/RoofLogic/include for the native env
```

**Structure Decision**: one new pure library beside `lib/DriverLogic`, split by responsibility
(STRUCT-03): the Hall filter and the motion profile are testable on their own, and the controller
composes them. Settings follow the existing ConfigModel pattern, one function per section in each
of the defaults, JSON in, JSON out and validation files.

## Phases

1. **Setup**: the library skeleton, the native include path, the test folders.
2. **Foundational**: types, settings and `validSettings`, Hall filter, motion profile, each with
   unit tests.
3. **US1** open/close end to end; **US2** faults and latch; **US3** unknown position; **US4** stop,
   abort, reversal: the controller, test-first, story by story.
4. **US5** the settings model, web schema, fixtures, catalogue.
5. **Property tests** (SC-001) over everything.
6. **Docs, demo core hash, contract schema, quality gate, full quickstart §1.**

## Complexity Tracking

None.
