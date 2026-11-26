# Tasks: Plausible Ends and a Moving Roof

- [x] T001 Replay the field run (B01) and the survey runs: estimate at the stray reading, sag at 25 %, the time guard
- [x] T002 Native tests first: `test/test_roof_end_check` (early stray, lasting reading, plausible point, clear level, normal moves, reversal and stop, unknown start, calibration legs, doubt and re-teach, restored fault, setting range)
- [x] T003 `FaultKind::EndTooEarly`, `Refusal::EndDoubted`, `EndCheck`/`EndCheckReport`, `Status::earlyEnd` (`RoofTypes`)
- [x] T004 The end check: `RoofControllerEnd.cpp`, the slow-duty hold in `driveProfile`, the dead-time arrival, the doubt in `followHall`, `requestMove`, `check`, `atEnd`, `clearFault`, `applySettings`
- [x] T005 `roof.endPlausiblePercent` through `RoofConfig`, JSON in/out, validation, `settingsFromConfig`, `validSettings`
- [x] T006 Fault and refusal texts (`RoofText`), stored-kind bounds (`FaultRecord`, `KeptSummaries`), the needs-person alert at once (`NeedsPerson`), the end-check log line (`RoofControlTask`)
- [x] T007 The Hall peak in the move summary (`MoveLog`, `DiagMoves`) and on Diagnostics
- [x] T008 `noEndMagnet` and `noMovement` (`RoofControllerWatch.cpp`), their settings, tests (shifted magnet, short move minimum, healthy roof, 25 % crawl, calibration window, off); the simulated roof's sag
- [x] T009 Existing tests adjusted where the new checks rightly change the outcome: hall-noise arrival after 3 s, P6 in the property test, the timeout and drive-budget tests (overrun off), the slower-roof test (130 %), the fault-record kind bound
- [x] T010 Web: schema, types, mocks, the Motion fields and the slow-duty note, the Diagnostics row and fault labels, i18n and the device catalogue
- [x] T011 Fixtures, settings-deps catalogue, contract schemas, OpenAPI, demo core
- [x] T012 Constitution 9.1.0, docs (roof motion, configuration, diagnostics, troubleshooting, REST), changelog, roadmap
- [x] T013 Firmware build, flash budget, quality gate, Playwright, docs build; PR with the bench checks

## Phase 2: Convergence

Assessed against spec.md (FR-001 to FR-016), plan.md and the constitution (9.1.0): every
requirement has code and a native or web test; nothing left to build. The doubt across a restart
once the fault is cleared, and the movement sag at the slow duty on the real roof, are recorded as
assumptions and bench checks, not gaps.

## Phase 3: Field fix (spec 040, F34)

- [x] T014 The movement sag counts in any reading after the surge, not held `CONFIRM_MS`: a full-speed open latched `noMovement` 21 s in with a 0.63 V deepest sag (`RoofControllerWatch.cpp`); the fault text names what was missing; native tests (`test/test_roof_motion_sag`) for a noisy sag, an occasional deep sag, a flat supply and the surge
