# Implementation Plan: Plausible Ends and a Moving Roof

**Branch**: `spec/036-plausible-end-arrival` | **Spec**: [spec.md](spec.md)

## Summary

`Roof::Controller` judges a target magnet against its own position estimate before calling it an
arrival (`RoofControllerEnd.cpp`). Early, the move is marked `early`, the profile drops to the slow
duty, and each step decides: accept (lasted 1 s and plausible or clearly at the magnet), wait, or
latch `FaultKind::EndTooEarly`. A move ending while early sets a doubt on that end: `followHall`
won't place the roof on it and `requestMove`/`check` refuse `Refusal::EndDoubted` toward it until the
other end's magnet or new Hall settings. `NeedsPerson` treats `EndTooEarly` like a Hall rail fault
(alert at once). `MoveLog` records the Hall peak. One setting, `roof.endPlausiblePercent`.

`RoofControllerWatch.cpp` adds the other two checks to every step of a move: at the drive's start
it stamps the overrun limit (from `expectedMoveMs` of the distance to go, moves from a known
position only) and the supply at rest; `checkOverrun` runs in `checkTimes` before the timeout, and
`checkMovement` after the electrical checks, restarting its window on a sag reading (spec 040; was held `CONFIRM_MS`) or a
Hall change. Three more settings: `roof.endOverrunPercent`, `roof.motionSagVolts`,
`roof.motionCheckSeconds`; the simulated roof sags 1.0 V at full duty, as the field roof does.

## Constitution check

- **I (Motion Safety)**: tightened, MINOR 9.0.0 → 9.1.0: "stop on its target magnet" now requires a
  plausible reading for moves from a known position. No principle relaxed; the slow-duty hold is at
  most 5 s inside the move's own timeout and drive budget, every other fault still checked. The
  calibration exception is untouched; moves from an unknown position keep their rule. The overrun
  and movement checks only stop a move earlier than the hard timeout would.
- **II/III**: unchanged. The automatic close is refused toward a doubted closed end (no motion is
  the safe side; the needs-person alert already holds).
- **IV (Alpaca)**: `ShutterOpen`/`Closed` only from the Open/Closed states, which an early reading
  never reaches; tested through `RoofIo::domeStatus`.
- **V**: all decisions in `lib/RoofLogic`; native tests `test_roof_end_check`, plus additions to
  `test_needs_person` and `test_diag_recorder`.
- **VI**: about 2 KB of flash; the web bundle grows by a few strings.
- **VII**: four number fields and one warning note in the existing Motion card pattern, one
  Diagnostics row; no new layout.
- **IX**: roof-motion, configuration, diagnostics, troubleshooting, REST docs and the changelog.

## Files

- `lib/RoofLogic`: `RoofTypes.h/.cpp` (fault, refusal, `EndCheck`), `RoofSettings.h/.cpp` (setting,
  constants), `RoofController.h/.cpp`, `RoofControllerStep.cpp`, new `RoofControllerEnd.cpp` and
  `RoofControllerWatch.cpp`; `lib/RoofPlantLogic` (the simulated sag).
- `lib/ConfigModel` (field, JSON in/out, validation), `lib/RoofIoLogic` (texts, fault record bound),
  `lib/CloseSequenceLogic/NeedsPerson`, `lib/DiagLogic` (peak, JSON, kept-summary bound).
- `src/RoofControlTask.cpp`, `include/RoofControl.h`: log each end check.
- Web: `roofSchema.ts`, `RoofTab.tsx`, `Diagnostics.tsx`, types, mocks, i18n.
- Fixtures and contracts: `config-ranges.json`, settings-deps catalogue and default config,
  `generate_schemas.py`, schemas, OpenAPI, demo core.
