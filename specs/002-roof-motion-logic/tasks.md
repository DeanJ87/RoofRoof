# Tasks: Roof State and Motion Logic

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/roof-logic.md](contracts/roof-logic.md),
[quickstart.md](quickstart.md)

**Tests**: The constitution requires native tests for all `lib/` logic (V) and a failing test for
every Principle I rule, so tests are written first in every story.

**Organisation**: the shared types, settings, Hall filter and profile are foundational (Phase 2).
Each story then adds the controller behaviour it needs, test first. Every task ends with
`pio test -e native -f "test_roof_*"` green; each phase ends with the plan's full local check.

## Phase 1: Setup

- [x] T001 Create `lib/RoofLogic/{include,src}` and add `-Ilib/RoofLogic/include` to `[env:native]` in `platformio.ini`
- [x] T002 [P] Create the native suites `test/test_roof_profile`, `test/test_roof_logic`, `test/test_roof_faults` and `test/test_roof_properties`, and `test/roof_sim/RoofSim.h` (shared by them) (the simulated roof: position from duty and speed, Hall counts from position, optional jam, reversed wiring, missing magnet)

## Phase 2: Foundational

- [x] T003 [P] Write `lib/RoofLogic/include/RoofTypes.h` and `src/RoofTypes.cpp`: `State`, `End`, `Command`, `Origin`, `HallLevel`, `FaultKind`, `Refusal`, `Fault`, `CommandResult`, `Readings`, `Output`, `Status` and the machine names exactly as in data-model.md
- [x] T004 [P] Write `lib/RoofLogic/include/RoofSettings.h` and `src/RoofSettings.cpp`: `Settings` with the defaults of data-model.md, the fixed values (`DEAD_TIME_MS = 500`, `HALL_CONFIRM_READINGS = 3`, `HALL_HYSTERESIS_COUNTS = 40`, `HALL_RAIL_LOW_COUNTS = 30`, `HALL_RAIL_HIGH_COUNTS = 4065`, `CONFIRM_MS = 250`, `MAX_STEP_GAP_MS = 100`) and `validSettings` (travel 100-20000, speed 1-500, margin 3-300 s, leave 2-120 s, ramps 0-30000 ms, top duty 20-100, slow duty 10-100 and ≤ top, thresholds 100-3995 with high ≥ low + 80, current 0.5-40 A, supply 6-15 V, finite floats)
- [x] T005 Test the Hall filter in `test/test_roof_profile/test_main.cpp`: unsettled until 3 equal readings, thresholds inclusive (`>=` high, `<=` low), hysteresis on leaving, rail at ≤ 30 / ≥ 4065, a changed candidate restarts the count
- [x] T006 Implement `lib/RoofLogic/include/HallFilter.h` and `src/HallFilter.cpp` to pass T005
- [x] T007 Test the profile in `test/test_roof_profile/test_main.cpp`: duty fraction ramp-up from slow to top, slow-down by distance, slow after the end, `toDuty` rounding and bounds, estimate advance clamped, `expectedMoveMs` against a step-by-step integration (within 1%), timeouts = expected + margin, unknown-move timeout = travel / slow speed + margin, worst valid settings below 2^31 ms
- [x] T008 Implement `lib/RoofLogic/include/MotionProfile.h` and `src/MotionProfile.cpp` to pass T007 (closed form after the ramp-up, research R4)

## Phase 3: User Story 1 - Open and close end to end (P1)

**Goal**: a closed roof opens and an open roof closes, ending on the magnet. **Independent test**: the simulated roof runs both moves with the default settings.

- [x] T009 [US1] Tests in `test/test_roof_logic/test_main.cpp`: boot at the closed magnet is `closed` and Off; open drives the open direction (and `openDirection = reverse` swaps it); the first driven step waits for the dead time; ramp-up linear from slow to top; slow-down near the end; open stops on the open magnet (Off on that step, `open`, position = travel); close stops on the closed magnet; open when open and close when closed are accepted no-ops; a repeated command leaves the move unchanged; end-to-end simulated open and close within their timeouts (SC-003)
- [x] T010 [US1] Implement `Controller` construction, `tick`, `command(Open/Close)`, arrival and idle Hall following in `lib/RoofLogic/include/RoofController.h`, `src/RoofController.cpp` and `src/RoofControllerStep.cpp` to pass T009

## Phase 4: User Story 2 - Every fault stops the motor and stays stopped (P1)

**Goal**: each fault kind stops the driver on its step and latches. **Independent test**: drive a simulated move into each fault.

- [x] T011 [US2] Tests in `test/test_roof_faults/test_main.cpp`: timeout at the limit and not one step before; didn't leave the start magnet at the leave time; wrong magnet (the other end's; the start magnet again after leaving); Hall rail low and high, moving and at rest; over-current only after 250 ms (a shorter spike is ignored) and only when current sense is on; current unreadable with current sense on; supply low and unreadable during a move after 250 ms; late step (gap 101 ms faults, 100 ms doesn't); `raiseFault`; every fault: Off on the step, `error`, position unknown, kind/measured/limit recorded; every open/close refused `faultLatched` from both origins; `clearFault` by automatic refused, by a person clears to the magnet's end or `unknown`; a fault restored before the first step latches
- [x] T012 [US2] Implement the fault checks, the latch, `raiseFault` and `clearFault` in `src/RoofControllerStep.cpp` and `src/RoofController.cpp` to pass T011

## Phase 5: User Story 3 - Never move by itself from an unknown position (P1)

**Goal**: unknown position refuses automatic moves; a person's move is slow and stops at the first magnet. **Independent test**: boot between the magnets.

- [x] T013 [US3] Tests: boot between the magnets is `unknown` and Off forever without a command; commands before the Hall settles refused `hallNotSettled`; automatic open/close refused `positionUnknown`; a person's close moves at no more than the slow duty with the unknown-move timeout and stops at the closed magnet; the other magnet first faults `wrongMagnet`; abort part-way makes the position unknown; leaving a magnet at rest makes it unknown; supply low or unreadable at rest refuses with the volts and the limit and latches nothing
- [x] T014 [US3] Implement the unknown-position rules and refusals in `src/RoofController.cpp` to pass T013

## Phase 6: User Story 4 - Stop part-way and carry on (P2)

**Goal**: stop keeps the estimate; a later move continues from it; reversal goes through the dead time. **Independent test**: stop a simulated move half-way and resume both ways.

- [x] T015 [US4] Tests: stop part-way is `stopped` with the estimate and Off on that step; resume open and close from `stopped` with the profile and a timeout for the distance left; reversing a move is Off for at least the dead time first; stop during the dead time returns to the previous idle state, abort then makes the position unknown; current is not checked during the dead time; automatic resume from `stopped` is allowed; `applySettings` refused while a move is active and taken when idle; invalid settings refuse every move `settingsInvalid`; the clock wrapping during a move
- [x] T016 [US4] Implement stop, abort, reversal and `applySettings` in `src/RoofController.cpp` to pass T015

## Phase 7: User Story 5 - Settings (P2)

**Goal**: the roof settings are part of the settings model with shared validation. **Independent test**: round-trip, ranges and migration.

- [x] T017 [P] [US5] Add `RoofConfig` to `lib/ConfigModel/include/Config.h` and `Config::roof`; defaults in `src/ConfigModel.cpp`; `appendRoof` in `src/ConfigJsonOut.cpp`; `applyRoof` (enums as strings, rejected with a message when unknown) and the whole-number fields in `checkWholeNumbers` in `src/ConfigJsonIn.cpp`; `validateRoof` in `src/ConfigValidate.cpp` with the messages of data-model.md
- [x] T018 [P] [US5] Add `Roof::settingsFromConfig` in `lib/RoofLogic/src/RoofSettings.cpp`, and a test that the defaults of `RoofConfig` map to `Roof::Settings()` and are valid
- [x] T019 [US5] Tests: `test/test_config_model` (round-trip, enum strings, cross rules and their messages, a stored config with all roof fields at their maximum stays under `MAX_PERSISTED_JSON_BYTES`), `test/test_config_migration` (a config with no `roof` loads with the defaults and current sense off), `test/test_config_backup` (the `roof` section survives backup and restore)
- [x] T020 [US5] Add the roof ranges to `test/fixtures/config-ranges.json`; regenerate `test/fixtures/settings-deps/default-config.json`; add every `roof.*` setting to `noDependency` in `lib/SettingsDeps/catalogue.json`; run `tools/settings-deps/check.py`
- [x] T021 [P] [US5] Web: `RoofConfig` in `web/src/types/config.ts`, `web/src/validation/roofSchema.ts` (ranges and the two cross rules with the device's messages) wired into `configSchema.ts` as optional `roof`, `roof` in `web/src/mocks/config.ts`, strings in `web/src/i18n/en.json` and `en.context.json`, tests in `web/src/__tests__/configSchema.test.ts`
- [x] T022 [US5] Regenerate the device message catalogue (`tools/i18n/gen_device_catalog.py`), the contract schema `config.schema.json` (`tools/contracts/generate_schemas.py`) and the OpenAPI document; run `node tools/i18n/check.mjs` and `literals.mjs`

## Phase 8: Safety properties (SC-001)

- [x] T023 Write `test/test_roof_properties/test_main.cpp`: 10,000 seeded sequences (xorshift, fixed seeds) of random commands, origins, settings, Hall noise, rails, supply dips, current spikes, jams, reversed wiring and missing magnets against `RoofSim`, checking after every step: never both directions and Off for the dead time between driven periods; duty ≤ top (≤ slow from unknown); Off while a fault is latched and until a person clears it; no driving that started from an unknown position without an accepted person command; no driven period longer than its timeout plus one step; Off on the step the target magnet is confirmed; failures print the seed and step

## Phase 9: Polish and cross-cutting

- [x] T024 [P] Docs: `docs/user-guide/roof-motion.md` (states, commands, profile, faults, the latch, the settings table) with the state machine as blocking diagram DIA-21 confirmed with `tools/docs/diagrams.py --confirm DIA-21`; nav in `mkdocs.yml`; the `roof` section in `docs/user-guide/configuration.md` and the `GET /api/config` example in `docs/api/rest.md`; `lib/RoofLogic` in `docs/development/heritage.md`
- [x] T025 Rebuild the demo core (`bash tools/demo-core/build.sh`) so `SOURCE_HASH` is current
- [x] T026 Run quickstart §1 of spec 001 (firmware build, native tests, web typecheck/test/build, Playwright, quality gate, flash budget, docs build) and fix anything it finds; run `tools/dashboard/check.py`
- [ ] T027 Commit in logical steps (`002: …`), rebase onto `origin/main`, re-run affected checks, push and open the PR with the summary, principles, check results, owner assumptions and the bench-check list

## Dependencies

- Phase 1 → Phase 2 → US1 → US2 → US3 → US4 (the controller grows story by story in the same files).
- US5 (Phase 7) depends only on Phase 2 (T004) and can run in parallel with US1-US4.
- Phase 8 needs US1-US4. Phase 9 needs everything.

## Parallel opportunities

- T003 and T004; T005-T006 and T007-T008 (different files).
- T017, T018 and T021 (device model, mapping, web) alongside the controller stories.
- T024 (docs) once the behaviour is settled.

## Implementation strategy

MVP is US1-US3 (all P1): the controller can open, close, fault safely and refuses to move blind.
US4 and US5 complete the scope; Phase 8 is the evidence for SC-001.

## Phase 10: Safety review fixes

- [x] T028 Write the failing tests first: the dead time timed by steps only, with a command handled between steps and the next step 2 ms later (`test_dead_time_is_timed_by_steps_only`), and the first drive after start-up (`test_the_first_drive_after_start_up_waits_the_dead_time`), in the new suite `test/test_roof_timing/test_main.cpp` (with the drive-budget, abort-at-rest and reading-sanity tests); P1 in `test/test_roof_properties/test_main.cpp` now times the dead time from the first step after the stop and from start-up
- [x] T029 Remove the time from `command` and `raiseFault`; time the dead time from the first step after the driver went off, construction included, in `lib/RoofLogic/src/RoofController.cpp` and `src/RoofControllerStep.cpp` (FR-002, FR-008)
- [x] T030 Rail readings counted in a window, 3 of the last 8, kept out of a magnet's run, in `lib/RoofLogic/src/HallFilter.cpp`; tests `test_hall_rail_readings_counted_in_a_window`, `test_isolated_rail_readings_do_not_interrupt_a_magnet`, `test_fault_hall_rail_alternating_with_good_readings`, `test_two_rail_readings_in_eight_are_a_glitch`; property P7 with intermittent-connector glitches (FR-019)
- [x] T031 Drive budget (`driveBudgetMs`, fault `driveBudget`) since the last magnet or a person's clear, in `lib/RoofLogic/src/MotionProfile.cpp` and `src/RoofControllerStep.cpp`; tests `test_repeated_restarts_hit_the_drive_budget`, `test_reaching_a_magnet_resets_the_drive_budget`; property P8 (FR-025)
- [x] T032 Reading sanity (`supplyReadable`, `currentReadable`) in `lib/RoofLogic/src/RoofSettings.cpp`; tests `test_reading_sanity_bounds`, `test_implausible_supply_readings_are_unreadable`, `test_negative_current_is_unreadable_with_current_sense_on`, `test_fault_supply_implausibly_high_during_a_move` (FR-026)
- [x] T033 Abort at rest keeps a known position: `test_abort_at_rest_keeps_a_known_position`; stated in the spec (FR-002) and the contract
- [x] T034 Spec, research (R11), data model, contract, `docs/user-guide/roof-motion.md` updated; DIA-21 re-checked against the sources and re-confirmed; the deliberate-breakage run extended to the new rules

## Phase 11: Re-review fixes

- [x] T035 Tests first in `test/test_roof_timing/test_main.cpp`: `test_a_reversal_near_the_end_never_trips_the_drive_budget` (the four reproduced cases plus duty extremes, tightest margin), `test_the_budget_still_trips_restarts_in_one_direction`, `test_a_step_clock_going_backwards_keeps_the_driver_off`; property P8 per direction and P8b `test_property_one_reversal_never_trips_the_drive_budget` in `test/test_roof_properties/test_main.cpp`
- [x] T036 Drive budget per direction in `lib/RoofLogic/src/RoofController.cpp` and `src/RoofControllerStep.cpp`; signed dead-time comparison in `waitingDeadTime` (FR-025, FR-008)
- [x] T037 Spec (FR-008, FR-025, assumptions), contract (guarantees 8, 9), research (R11a), data model and user guide updated; breakages for a shared budget and an unsigned dead time added to the run

