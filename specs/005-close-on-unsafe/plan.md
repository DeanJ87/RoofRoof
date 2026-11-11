# Implementation Plan: Close on Unsafe

**Branch**: `spec/005-close-on-unsafe` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/CloseSequenceLogic`: the pure sequence (`CloseSeq::Sequence`): phases, time limits, park
  request spacing, a person's stop, unknown position and faults, the events and their alert wording.
- Firmware: `CloseOnUnsafe` gathers the inputs on the main loop, carries out the decision (park
  request through `SafetyInputs`, close and stop through `RoofControl::command` with
  `Origin::Automatic`), logs every change and hands critical events to `WebServer::raiseRoofAlert`.
- `RoofControl` counts a person's stops in its snapshot; `Alerts::AlertType::Roof` is the alert
  hook spec 007 replaces.
- Setting `park.timeoutSeconds` through ConfigModel, the web Zod schema, the range fixture and the
  docs.

Decisions are in [research.md](research.md); types and the transition table in
[data-model.md](data-model.md); the offered interfaces in [contracts/](contracts/).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | `lib/RoofLogic` (`Roof::State`), spec 003's `RoofControl`, spec 004's `SafetyInputs`, the alert dispatcher |
| **Storage** | NVS: one integer in the main config document |
| **Testing** | `pio test -e native` (new: `test_close_sequence`, `test_close_standdown`, `test_close_text`, `test_close_properties`, sharing `test/close_rig`; extended: `test_config_safety`, `test_config_ranges`, `test_alert_clients`, `test_settings_deps` fixture), Vitest (config schema), a deliberate-breakage run by hand (results in the PR) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + settings schema + docs |
| **Performance Goals** | the sequence is stepped every main-loop pass (~10 ms); a decision is a few comparisons; no allocation except alert text |
| **Constraints** | no blocking on the main loop; flash within SIZE-04 (warn > 90 %, fail > 95 % of the OTA slot); growth recorded if over 15 KB |
| **Scale/Scope** | ~350 lines of lib logic, ~900 lines of tests, ~150 lines of glue |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | The sequence commands nothing while the roof is unknown or in error (also enforced by spec 002, which refuses an automatic move from unknown and any move with a fault). Every close is spec 002's move with its timeout; the guard only adds a stop. A person's stop stands the sequence down. | `test_close_sequence`: `test_unknown_*`, `test_fault_*`, `test_person_stop_*`; property P4, P5 |
| II. Parked mount | The close goes through `RoofControl::command`: spec 003's gate (park at the start) and interlock (throughout). The sequence adds a 4 s hold before commanding. Park not confirmed: stay open, critical alert. | `test_park_settle_*`, `test_park_timeout_*`, `test_park_refused_by_gate_*`, `test_park_lost_*`; property P2, P6 |
| III. Unsafe ends closed | Not safe (including unknown) starts the sequence: park, wait, close; every step has a time limit and a timed-out step stops where it is and alerts naming the step. No open in the action set. Alerts don't go through N.I.N.A. or SQMeter. Every alert text carries the park result's, fault's or refusal's text. | `test_start_*`, `test_timeout_*`, `test_close_guard_*`, `test_alert_text_*`; property P1, P3 |
| IV. Alpaca | Untouched (006 uses the same refusal). | - |
| V. Pure logic | All decisions in `lib/CloseSequenceLogic`, time passed in; unit tests at each boundary, a property test, deliberate breakage. | all of the above |
| VI. Budgets | Main loop only, no blocking; no new task; flash measured. | build |
| VII. UI | No UI (settings row is 009); Zod schema only. | Vitest |
| VIII. Security | No new endpoint. Automatic commands use `Origin::Automatic`, which can't clear a fault or move from unknown. | - |
| IX. Docs | New close-sequence section with a safety diagram (`blocking: true`); configuration, alerts and safety-input pages updated. | `diagrams.py`, `mkdocs build --strict` |
| X. Quality | check.py; functions ≤ 60 lines, complexity ≤ 15. | check.py |
| XI. Heritage | `AlertEngine.h`'s type list (copied from SQMeter) gains `Roof`: recorded as diverged. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/005-close-on-unsafe/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/close-sequence.md
└── checklists/requirements.md

lib/CloseSequenceLogic/
  library.json
  include/CloseSequence.h      Inputs, Decision, Phase, Action, Event, Cause, Sequence, alertFor
  src/CloseSequence.cpp        the transitions
  src/CloseSequenceText.cpp    names and alert wording
lib/AlertLogic/                AlertType::Roof ("roof")
lib/DeviceCore/src/AlertDelivery.cpp   ntfy tag for Roof
lib/ConfigModel/               park.timeoutSeconds
include/CloseOnUnsafe.h, src/CloseOnUnsafe.cpp   main-loop glue
include/RoofControl.h, src/RoofControl.cpp       count a person's stops (RoofIo::Snapshot::personStops)
include/WebServer.h, src/WebServerAlerts.cpp     raiseRoofAlert()
src/main.cpp                                      begin, updateConfig, handle
test/close_rig, test/test_close_sequence, test/test_close_standdown, test/test_close_text, test/test_close_properties
web/src/validation/configSchema.ts, web/src/types/config.ts, web/src/mocks/config.ts
test/fixtures/config-ranges.json
docs/user-guide/close-on-unsafe.md (new), safety-inputs.md, alerts.md, configuration.md, mkdocs.yml
```

**Structure Decision**: a new pure library beside `RoofIoLogic` and `SafetyInputLogic`, so the
sequence is tested on its own and the demo (spec 011) can compile it.

## Phases

1. **Pure logic first (tests first)**: unit tests for every transition and boundary, then the
   sequence; the property test; alert wording.
2. **Settings**: `park.timeoutSeconds` end to end.
3. **Firmware**: `personStops`, `AlertType::Roof`, `raiseRoofAlert`, `CloseOnUnsafe`, wiring; zero
   warnings; flash.
4. **Docs**: user page with the diagram, references, heritage, CHANGELOG.
5. **Verify**: deliberate breakage, quickstart §1 of spec 001, demo core, contract schemas, converge,
   independent safety review.

## Complexity Tracking

None.
