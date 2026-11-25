# Tasks: A Person Can Nudge the Roof Without the Hall Sensor

**Input**: spec.md, plan.md

**Tests**: required (constitution V): native tests for every lib change.

## Phase 1: Setup

- [X] T001 Constitution 9.0.0: Principle I's person's blind jog, with its risk

## Phase 2: US1 nudge without the Hall sensor (P1)

- [X] T002 [P] Native tests `test/test_roof_jog_blind`: allowed for a person either way, refused for other origins, keep-alive stop, jog limit, release/Stop/Abort, other faults refuse, settings and power refuse, supply drop stops it, never taken over but yields to the blind close, before the Hall settles
- [X] T003 Controller: `hallBlind()`, blind jog through `startBlind()`/`stepBlind()`, shared keep-alive, `blindReadiness()`, Moving refusals while one runs, endBlind's state and report (lib/RoofLogic)
- [X] T004 `StartInputs::jog` and `attendedRefusal` pass the Hall rail fault for a jog; teach test (lib/CalibrationLogic, test/test_calibration_teach)
- [X] T005 Log a blind jog's start as a warning (src/RoofTeach.cpp)
- [X] T006 [P] Web: `moveGate` drops the Hall blockers for a jog and flags it blind; `JogPanel` warning Note; wizard screens pass it on; i18n; Vitest (TeachCard, WizardMotor)
- [X] T007 Demo core rebuilt (`tools/demo-core/build.sh`)

## Phase 3: US2 override the end magnets (P1)

- [X] T010 [P] Native tests: a normal jog stops at the end magnet, an override jog carries on past it, unknown afterwards until a move reaches a magnet, person only, faults refuse, keep-alive and limit, new settings trust the magnets; `parseJog` and the teach start with the override
- [X] T011 Controller `ignoreMagnets` and `doubtMagnets`; `JogRequest::ignoreMagnets`, `StartInputs::ignoreMagnets`; firmware and demo core pass it
- [X] T012 [P] Web: "Don't stop at the end magnets" Toggle and warning; `startJog(direction, ignoreMagnets)`; Vitest
- [X] T013 API: operations.json, openapi.json, rest.md

## Phase 4: Polish

- [X] T008 Docs: user guide (roof motion, calibration/jog), troubleshooting of a dead Hall sensor
- [X] T009 Quickstart §1 checks: native, firmware build, web, Playwright, quality gate, flash budget, docs build
