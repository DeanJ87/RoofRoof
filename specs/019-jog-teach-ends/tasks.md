# Tasks: Jog and Teach the Ends

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/jog-teach-api.md](contracts/jog-teach-api.md)

Tests come first for every `lib/` change (constitution V). `[P]`: can run in parallel.

## Phase 1: Setup

- [x] T001 Roadmap entry for 019 and the optional-switch wording in `specs/ROADMAP.md`
- [x] T002 Constitution 2.0.1: Principle II wording and the Sync Impact Report in `.specify/memory/constitution.md`

## Phase 2: Foundational (pure logic, tests first)

- [x] T003 Native tests for the jog in `test/test_roof_jog/test_main.cpp`: slow duty both directions, hold deadline (499/500 ms, renewal moves it, wrong id, ended jog), deadline during the dead time, timeout at `min(JOG_MAX_MS, unknown timeout)`, budget across jogs, first magnet after leaving the start one (either level, no `wrongMagnet`), `didNotLeave` into the start magnet's end, person only, fault/moving/readiness refusals, position unknown after a stop part-way, `jogging`/`calibrating` flags, Stop and Abort
- [x] T004 Jog in `lib/RoofLogic` (`RoofTypes.h`, `RoofSettings.h`, `RoofController.h`, `RoofControllerCalibrate.cpp`, `RoofControllerStep.cpp`, `RoofController.cpp`)
- [x] T005 [P] `fitHall` factored out of `proposeHall` in `lib/CalibrationLogic/src/CalibrationProposal.cpp` (016 proposal tests unchanged)
- [x] T006 [P] `attendedRefusal` factored out of `Run::startRefusal` in `lib/CalibrationLogic/src/CalibrationRun.cpp` (016 run tests unchanged)
- [x] T007 Native tests for the teach in `test/test_calibration_teach/test_main.cpp`: jog start refusals (each, both directions), renewal, window readiness and reset, mark refusals, `proposeTeach` (halfway point, window clamp, samePole, weakEnd, noRoom, gap), confirm checks, clear, the document
- [x] T008 `Calib::Teach`, `proposeTeach`, `writeTeachDocument` in `lib/CalibrationLogic/include/Teach.h`, `src/Teach.cpp`, `src/TeachDocument.cpp`
- [x] T009 `jogging` in `lib/RoofIoLogic` `CommandCheck` and `openMustStop` in `RoofGates`, with tests in `test/test_roof_check` and `test/test_roof_gates`

## Phase 3: User Story 1-3 (firmware)

- [x] T010 `RoofControl`: the `Teach` under the lock, its window after each step, `jog`, `holdJog`, `teachMark`, `teachConfirmable`, `teachClear`, `teachReport`; the automatic close stops a jog; `jogging` in the command context (`include/RoofControl.h`, `src/RoofTeach.cpp`, `src/RoofControl.cpp`, `src/RoofControlTask.cpp`)
- [x] T011 Routes in `src/WebServerTeach.cpp` and `include/WebServer.h`: `/api/roof/jog`, `/api/roof/teach`, `/teach/mark`, `/teach/confirm` (save via `saveAsFlashWrite`), `/teach/discard`
- [x] T012 Firmware builds with zero warnings (`esp32dev`, `esp32dev_bench`); flash budget

## Phase 4: User Story 5 (demo)

- [x] T013 The teach in `tools/demo-core` (`emulated_roof.*`, `emulated_device*.{h,cpp}`, `bridge.cpp`); WASM rebuilt
- [x] T014 Demo handlers and `coreApi` in `web/src/demo`; MSW mocks

## Phase 5: User Story 1-4 (web)

- [x] T015 Types, API client and hold hook (`web/src/types/teach.ts`, `lib/teachApi.ts`, `hooks/useHold.ts`) with Vitest
- [x] T016 Teach card on the calibration page (`components/calibrate/TeachCard.tsx`, `Calibrate.tsx`) with Vitest
- [x] T017 Wizard: a Teach page before the Calibration run page; the optional switch note on the park inputs page; Vitest
- [x] T018 i18n texts and context notes; device text catalogue
- [x] T019 Playwright: jog, mark, confirm in the demo; axe at phone width; a11y inventory; screenshots

## Phase 6: Interfaces and docs

- [x] T020 `tools/api/routes.json`, `operations.json`, examples, `docs/api/openapi.json`, contract schemas
- [x] T021 Docs: `docs/hardware/calibration.md`, `commissioning.md`, `commissioning-checklist.md`, `docs/api/rest.md`, `docs/user-guide/calibration-run.md`, `setup-wizard.md`, `safety-inputs.md`; CHANGELOG

## Phase 7: Polish

- [ ] T022 spec 001 quickstart §1 in full; demo core hash; rebase; PR with the bench checklist
- [ ] T023 Converge; independent reviews of the motion and safety logic until safe to merge

## Phase 8: Convergence

- [x] T024 Make data-model.md and plan.md say how open and close are refused during a jog (`Status::jogging` in `checkCommand`, not a `CommandContext` field) per plan: lib/RoofIoLogic decision (contradicts)
- [x] T025 Add the release (`releaseJog`, `Teach::release`, `release: true`), `Teach::yieldToClose` and the start answer's `limitMs` to data-model.md per FR-002 (partial)
