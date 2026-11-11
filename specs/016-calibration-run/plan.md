# Implementation Plan: Calibration Run

**Branch**: `spec/016-calibration-run` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/RoofLogic`: a calibration leg in `Roof::Controller` (`checkCalibrate`, `calibrate`,
  `calibrationReport`): a person's move toward one end at a fixed duty profile, with its own hard
  timeout and drive budget, stopping at the first magnet after it leaves its start magnet (or the
  expected level), through the same step, dead time, latch and electrical checks as every move.
- `lib/CalibrationLogic` (new): the run (`Calib::Run`: start refusals, the legs and their limits,
  the waits, the endings), the statistics (`Calib::Stats`, `Calib::LegSamples`), the proposal
  (`Calib::propose`) and the JSON writer of the status document.
- `lib/RoofIoLogic`: `CommandContext::calibrating` (open and close refused with `calibrating`),
  `PlatformState::calibrating` (held as if moving), the status document's `calibrating`.
- `lib/ConfigModel`: `roof.calibrationLimitSeconds`.
- Firmware: `RoofControl` owns the run under its lock and steps it after each control step; the
  web server's three routes; `PlatformGuard::sequence()` for the start check.
- Demo core: the same run in the emulated roof, three bridge calls, the demo's handlers.
- Web: the `/calibrate` page, the Calibrate card in Settings → Roof, the wizard's Calibration run
  page, the Stop bar while calibrating, types, schema, mocks, i18n, tests.
- Route registry, OpenAPI, contract schemas; docs (a Calibration run guide, calibration,
  commissioning stages 3 and 8 and the checklist, quick bench test, REST, configuration),
  CHANGELOG, roadmap.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the routes
in [contracts/calibration-api.md](contracts/calibration-api.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 002's `Roof::Controller`; spec 003's `RoofControl`, gates and blockers; spec 005's close sequence (platform state); spec 010's `PlatformGuard`; spec 011's plant and demo core; spec 015's wizard |
| **Storage** | NVS: one value in the main config document; the run itself in RAM only |
| **Testing** | `pio test -e native` (new `test_roof_calibrate_leg`, `test_calibration_run`, `test_calibration_proposal`; `test_config_model`, `test_config_ranges`, `test_roof_check`, `test_platform_hold` extended); Vitest; Playwright (`tests/calibrate.spec.ts`, axe at phone width) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | the run's step is O(1) per control step (a bucket add); no allocation in the step |
| **Constraints** | no blocking on the async task (the status copies a small report under the roof's lock); motion task stack headroom kept; flash within SIZE-04; UI growth recorded |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Each leg is a controller move: one output writer, dead time, the leg's hard timeout and budget in the motion code, the existing faults and latch; above the slow duty only from a magnet; first magnet stop; nothing resumes after a restart; platform actions held for the whole run | `test_roof_calibrate_leg`, `test_calibration_run`, `test_platform_hold` |
| II. Parked mount | Park confirmed at the start and at each close leg (the close gate); the existing interlock during each close leg; park lost ends the run | `test_calibration_run` (park refused, park lost while opening, interlock while closing) |
| III. Unsafe ends closed | Refused unless safe; open legs gated; an opening leg stopped and the run ended when it turns; a closing leg finishes; the automatic close ends the run; refusals carry the existing texts | `test_calibration_run`, `test_roof_check` |
| IV. Alpaca | Alpaca can't start a run; its open/close is refused with `calibrating` during one; ConformU unaffected (no run in the simulator) | `test_roof_check`, ConformU CI |
| V. Pure logic | Leg, run, statistics, proposal, refusals, document in `lib/`, tests first | native suites |
| VI. Budgets | No allocation per step; the report copied under the lock is small; flash and UI measured | build, `flash_budget.py`, `size_check.py` |
| VII. UI | Shared cards and controls; Stop always shown during a run; reasons for disabled Start | Vitest, Playwright + axe |
| VIII. Security | Start and abort call `requireAuth` and need `X-RoofRoof`; status is readings only | `routes.py`, contract tests |
| IX. Docs | Guide, calibration, commissioning, bench test, REST, configuration; OpenAPI regenerated | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py, function limits | check.py |
| XI. Heritage | No SQMeter module changed | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/016-calibration-run/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
lib/RoofLogic/ include/RoofTypes.h, RoofController.h, RoofSettings.h, MotionProfile.h;
  src/RoofController.cpp, RoofControllerStep.cpp, RoofControllerCalibrate.cpp (new), MotionProfile.cpp, RoofTypes.cpp
lib/CalibrationLogic/ (new) include/CalibrationRun.h, CalibrationStats.h, CalibrationProposal.h, CalibrationDocument.h;
  src/CalibrationRun.cpp, CalibrationRunLegs.cpp, CalibrationStats.cpp, CalibrationProposal.cpp, CalibrationDocument.cpp
lib/RoofIoLogic/ CommandCheck, PlatformHold, RoofStatus, RoofText, SettingsGuard
lib/ConfigModel/ Config.h, ConfigJsonIn.cpp, ConfigJsonOut.cpp, ConfigValidate.cpp
include/RoofControl.h, src/RoofControl.cpp, src/RoofControlTask.cpp, src/RoofCalibration.cpp (new),
  include/WebServer.h, src/WebServerRoof.cpp, include/PlatformGuard.h, src/PlatformGuard.cpp
tools/demo-core/ emulated_roof.*, emulated_device*.{h,cpp}, bridge.cpp
test/test_roof_calibrate_leg, test/test_calibration_run, test/test_calibration_proposal (new)
web/src: components/Calibrate.tsx, components/calibrate/* (new), hooks/useCalibration.ts (new),
  lib/calibrationApi.ts (new), settings/RoofTab.tsx, wizard pages, RoofStopBar/roofView, App.tsx,
  types, validation, demo handlers, mocks, i18n
web/tests/calibrate.spec.ts (new), a11y inventory
tools/api/{routes,operations}.json, tools/contracts/generate_schemas.py, docs/api/openapi.json
docs/user-guide/calibration-run.md (new), mkdocs.yml, docs/hardware/{calibration,commissioning,
  commissioning-checklist,quick-bench-test}.md, docs/api/rest.md, docs/user-guide/configuration.md, CHANGELOG.md
```

**Structure Decision**: the leg is a mode of the one controller (so every rule and fault applies
to it); the run is a separate pure state machine that only asks the controller for legs, so it
can be tested end to end against the plant without hardware, and the firmware and the demo share
it.

## Phases

1. **Pure logic, tests first**: the controller's leg; the statistics and proposal; the run.
2. **Settings and checks**: the setting; `calibrating` in the command check, platform hold and
   status.
3. **Firmware glue**: RoofControl, routes, PlatformGuard; zero warnings; flash.
4. **Demo core**: the run in the emulated roof; WASM rebuilt.
5. **Web**: page, card, wizard page, Stop bar; Vitest and Playwright; contracts, OpenAPI.
6. **Docs**.
7. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
