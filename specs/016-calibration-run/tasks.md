# Tasks: Calibration Run

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).
Web logic in Vitest; pages in Playwright.

**Organisation**: the pure logic first (leg, statistics, proposal, run), then the checks and the
setting, firmware, demo, web and docs.

## Phase 1: Setup

- [ ] T001 Add "016" to `specs/ROADMAP.md`

## Phase 2: Tests first

- [ ] T002 [US1] [US4] `test/test_roof_calibrate_leg`: refusals (not a person, fault, moving, readiness, invalid leg each bound, above slow duty off a magnet); the dead time first; drives at the leg's duty in the leg's direction (both open directions); leaves its start magnet and stops at the first magnet (either level with `expect` any); `expect` the other level faults `wrongMagnet`; still on the start magnet is fine until the leave time, then `didNotLeave`; timeout at exactly the limit (`>=`); the budget is the limit and counts across stopped legs; supply, over-current, rail and late step still fault; the top-duty profile (ramp up, slow after `slowAfterMs`, ramp down); `dutyMs` and `drivenMs`; the report for arrived, stopped, faulted and replaced legs; `calibrating` in the status; a normal command while a leg runs (same target accepted, the other replaces it); the estimate kept from a magnet, unknown from part-way
- [ ] T003 [US1] [US5] `test/test_calibration_proposal`: stats (mean, sigma, merge, min/max); leg samples (middle half, width doubling, short legs); speed from slow legs and from top legs, rounding, out of range; disagreement > 30 % withheld, > 15 % warned; a leg under 3 s; margin's three terms and rounding and range; polarity both ways; thresholds kept, raised for noise, lowered for the rest reading, no room; same pole; weak end; the range and gap limits; current warning at 80 %
- [ ] T004 [US1]-[US4] `test/test_calibration_run`: against the plant at 20 ms steps: a full run from closed, open and part-way; with the top pass; each start refusal and its order; every leg's limit; the 1 s settle and its rest readings; a stop mid-leg and in a pause (no proposal, settings unchanged); the weather turning while opening (stopped), while closing (finishes, then ends), in a pause; daylight; the park lost while opening and in a pause, and the interlock while closing (a fault); blockers wait then fail at 15 s; a missing magnet (timeout), a jam with current (over-current), a backwards motor (didNotLeave), low supply, a rail; the polarity opposite to the setting proposes the other end; same-pole magnets end at the closed end with no thresholds; the automatic close yields; `wantsLeg`

## Phase 3: Pure logic

- [ ] T005 `lib/RoofLogic`: the calibration leg until T002 passes
- [ ] T006 `lib/CalibrationLogic`: stats, samples and the proposal until T003 passes
- [ ] T007 `lib/CalibrationLogic`: the run and the document writer until T004 passes

## Phase 4: Checks and the setting

- [ ] T008 `lib/RoofIoLogic`: `CommandContext::calibrating` (`test_roof_check`), `PlatformState::calibrating` (`test_platform_hold`), `Snapshot::calibrating` in the status (`test_roof_status`), `settingsSaveRefused` with a run
- [ ] T009 `lib/ConfigModel`: `roof.calibrationLimitSeconds` (in, out, validate, default, range fixture, settings-deps catalogue if it needs an entry); `test_config_model`, `test_config_ranges`, `test_config_migration`

## Phase 5: Firmware

- [ ] T010 `RoofControl`: the run under the lock, stepped after the tick; start, stop, report; `moving()`, `whileStill`, `settingsSaveRefused`, `automaticClose` yields, stops end the run, open/close refused; snapshot `calibrating`
- [ ] T011 `PlatformGuard::sequence()`; `WebServer` routes (start with its body, abort, status) registered before shorter routes; `pio run -e esp32dev` and `-e esp32dev_bench` with zero warnings; flash baseline

## Phase 6: Demo core

- [ ] T012 `tools/demo-core`: the run in the emulated roof (step, command context, automatic close, stops), bridge calls; `bash tools/demo-core/build.sh`

## Phase 7: Web

- [ ] T013 Types, zod schema for the setting, mocks (config, the three routes), demo handlers, `calibrationApi.ts`, `useCalibration`
- [ ] T014 The `/calibrate` page: checks, start form, progress with Stop always shown, result with the proposal table, Accept and save through the settings editor, Discard
- [ ] T015 Settings → Roof Calibrate card; the wizard's Calibration run page after Hall; the Stop bar while `roof.calibrating`
- [ ] T016 i18n texts and context notes; Vitest (proposal rows, the page's states, Stop never disabled, the wizard page has no motion control, the card); Playwright `tests/calibrate.spec.ts` (demo run, stop, accept; axe at phone width); inventory; UI size baseline if needed
- [ ] T017 Route registry, operations, OpenAPI, contract schemas (calibration document, config, status)

## Phase 8: Docs

- [ ] T018 `docs/user-guide/calibration-run.md` (new, in the nav); `docs/hardware/calibration.md`; commissioning stages 3 and 8 and the checklist; quick bench test; REST; configuration; CHANGELOG

## Phase 9: Verify

- [ ] T019 quickstart §1 of spec 001 (build, native, web, Playwright, quality, flash, docs); converge; independent reviews until safe to merge; rebase; `source_hash.py --check`; PR
