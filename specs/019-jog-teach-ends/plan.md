# Implementation Plan: Jog and Teach the Ends

**Branch**: `spec/019-jog-teach-ends` | **Date**: 2026-11-18 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/RoofLogic`: a **jog** in `Roof::Controller` (`checkJog`, `jog`, `holdJog`, `jogId`,
  `Status::jogging`). It is a calibration leg (spec 016) at the slow duty that stops at the first
  magnet after leaving its start magnet, with three differences: its hard timeout is
  `min(JOG_MAX_MS, unknownMoveTimeoutMs)`, its drive budget is every move's (`driveBudgetMs`), and
  the step stops it when `holdJog` hasn't renewed it for `JOG_HOLD_MS` of step time. A jog never
  keeps a position estimate.
- `lib/CalibrationLogic`: the threshold rule shared by the run and the teach (`fitHall`, factored
  out of `proposeHall`); the start checks shared by the run and the jog (`attendedRefusal`,
  factored out of `Run::startRefusal`); and a new `Calib::Teach`: the jog's start and renewal
  checks, the one-second reading window, the three marks, the proposal (`proposeTeach`) and the
  document writer for `GET /api/roof/teach`.
- `lib/RoofIoLogic`: open and close refused with `jogging` while `Status::jogging`,
  `openMustStop` also stops a jog in either direction. The interlock already watches every
  calibration-type move, so it covers a jog.
- Firmware: `RoofControl` owns the `Teach` under its lock and steps its window after each control
  step; the automatic close stops a jog first; the web server's five routes (`/api/roof/jog`,
  `/api/roof/teach`, `/teach/mark`, `/teach/confirm`, `/teach/discard`). The confirm saves through
  `saveAsFlashWrite`.
- Demo core: the same `Teach` in the emulated roof, one bridge call, the demo handlers.
- Web: a Teach card at the top of `/calibrate` (hold-to-run buttons with a keep-alive, marks,
  proposal), a wizard page before the Calibration run page, the optional note on the wizard's park
  inputs, types, API client, mocks, i18n, Vitest, Playwright.
- Constitution 2.0.1 (Principle II wording), roadmap entry, route registry, OpenAPI, contract
  schemas; docs (calibration, commissioning stage 3 and checklist, REST, setup wizard, safety
  inputs), CHANGELOG.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); routes in
[contracts/jog-teach-api.md](contracts/jog-teach-api.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 016's calibration leg, statistics and proposal; spec 003's `RoofControl`, gates and blockers; spec 005's close sequence; spec 010's `PlatformGuard`; spec 011's plant and demo core; spec 015's wizard |
| **Storage** | none new: the marks and the jog live in RAM; the confirm saves three existing settings |
| **Testing** | `pio test -e native` (new `test_roof_jog`, `test_calibration_teach`; `test_calibration_proposal`, `test_roof_gates`, `test_roof_check` extended); Vitest; Playwright (`tests/calibrate.spec.ts` extended) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | the window is a 50-entry ring, O(1) per step; no allocation in the step |
| **Constraints** | flash at about 82.7 % before this spec: keep additions lean; no blocking on the async task |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | A jog is a controller move: one output writer, dead time, its hard timeout and the keep-alive deadline in the step, the drive budget, every fault and the latch; slow duty; first magnet stop; memory only | `test_roof_jog` |
| II. Parked mount | Park needed for both directions; the interlock watches every jog; wording fix (2.0.1, PATCH) without changing `ParkRule` | `test_roof_jog`, `test_calibration_teach`, `test_park_logic` unchanged |
| III. Unsafe ends closed | Both directions need the safe weather and no daylight close; a jog stops when either turns; refusals carry the existing texts | `test_roof_gates`, `test_calibration_teach` |
| IV. Alpaca | Alpaca can't jog; its open/close is refused with `jogging` during one | `test_roof_check` |
| V. Pure logic | Jog, window, marks, proposal, refusals, document in `lib/`, tests first | native suites |
| VI. Budgets | 100-byte ring, small report copied under the lock; flash and UI measured | build, `flash_budget.py`, `size_check.py` |
| VII. UI | Shared cards and buttons; Stop always shown while the roof moves; reasons for disabled buttons | Vitest, Playwright + axe |
| VIII. Security | Every changing route calls `requireAuth` and needs `X-RoofRoof`; the status is readings only | `routes.py`, contract tests |
| IX. Docs | Calibration, commissioning, REST, wizard, safety inputs; OpenAPI regenerated | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py, function limits | check.py |
| XI. Heritage | No SQMeter module changed | - |

**Gate**: pass, no exceptions. The jog's timeout is never longer than an unknown-position move's,
so it doesn't use the calibration run's exception to Principle I.

## Project Structure

```text
specs/019-jog-teach-ends/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
.specify/memory/constitution.md (2.0.1), specs/ROADMAP.md
lib/RoofLogic/ RoofTypes.h, RoofSettings.h, RoofController.h; RoofControllerCalibrate.cpp,
  RoofControllerStep.cpp, RoofController.cpp, RoofTypes.cpp
lib/CalibrationLogic/ CalibrationProposal.{h,cpp} (fitHall), CalibrationRun.{h,cpp} (attendedRefusal),
  Teach.h, Teach.cpp, TeachDocument.cpp (new)
lib/RoofIoLogic/ CommandCheck, RoofGates
include/RoofControl.h, src/RoofControl.cpp, src/RoofTeach.cpp (new), include/WebServer.h,
  src/WebServerRoof.cpp, src/WebServerTeach.cpp (new)
tools/demo-core/ emulated_roof.*, emulated_device*.{h,cpp}, bridge.cpp
test/test_roof_jog, test/test_calibration_teach (new)
web/src: components/Calibrate.tsx, components/calibrate/TeachCard.tsx (new), hooks/useHold.ts (new),
  lib/teachApi.ts (new), types/teach.ts (new), wizard pages, demo handlers, mocks, i18n
web/tests/calibrate.spec.ts
tools/api/{routes,operations}.json, tools/contracts/generate_schemas.py, docs/api/openapi.json
docs/hardware/{calibration,commissioning,commissioning-checklist}.md, docs/api/rest.md,
  docs/user-guide/{calibration-run,setup-wizard,safety-inputs}.md, CHANGELOG.md
```

**Structure Decision**: the jog is a mode of the one controller, sharing the calibration leg's
step, so every rule and fault applies to it and the keep-alive deadline is kept by the step's
clock. The teach is a small pure class next to the run, so the firmware and the demo share it.

## Phases

1. **Pure logic, tests first**: the controller's jog; `fitHall` and `attendedRefusal` refactors;
   `Teach`.
2. **Checks**: `jogging` in the command check; `openMustStop` for a jog.
3. **Firmware glue**: RoofControl, routes; zero warnings; flash.
4. **Demo core**: the teach in the emulated roof; WASM rebuilt.
5. **Web**: Teach card, hold buttons, wizard page and note; Vitest and Playwright; contracts,
   OpenAPI.
6. **Constitution, roadmap, docs**.
7. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
