# Implementation Plan: A Person Can Nudge the Roof Without the Hall Sensor

**Branch**: `spec/033-person-nudge-without-hall` | **Spec**: [spec.md](spec.md)

## Summary

`Roof::Controller::jog` runs a person's jog **blind** when the Hall sensor can't place the roof
(`hallBlind()`: the Hall rail fault is the latched one, or no fault and the reading at a rail or not
settled). A blind jog reuses the blind close's drive (spec 030, `RoofControllerBlind.cpp`): `Blind`
gains a `target` and a `jog` flag, `startBlind()` is shared, `stepBlind()` checks the jog's keep-alive
deadline (now one controller-level `jogHoldPending`/`jogHoldUntilMs` for both kinds of jog) and drives
toward either end. The readiness without the Hall (`blindReadiness()`: settings, supply, current
sense) is shared by the blind close, the blind jog and `readinessRefusal()`. `Calib::attendedRefusal`
lets a jog's start past the Hall rail fault (`StartInputs::jog`, set by `Teach::jog`), the controller
decides. The web jog panel drops the Hall blockers for a jog and shows a warning Note.

## Technical Context

C++17 firmware (lib pure, Unity native tests), Preact + TypeScript UI, as in spec 001. Budgets: flash
about 89.8% of the OTA slot on main; LittleFS near its 75% limit and the CSS file's block nearly
full, so no CSS change (one i18n key only).

## Constitution Check

| Principle | How it is kept |
|---|---|
| I Motion Safety | Constitution 9.0.0 adds the person's blind jog as a stated exception with its risk: person only, hold to run with the step-clock deadline, slow duty, the jog limit (at most 30 s), the supply-sag stall stop, every fault still checked; the Hall fault stays latched, position unknown. Every other fault still refuses. Native tests (test_roof_jog_blind, test_calibration_teach). |
| II Parked mount | The jog's park gate is unchanged (spec 019); the park interlock still stops it. |
| III Unsafe ends closed | Weather and daylight gates unchanged; the automatic close and the blind close take over a blind jog. |
| IV Alpaca | Not touched: Alpaca can't jog. |
| V Testable logic | All decisions in lib/RoofLogic and lib/CalibrationLogic with native tests. |
| VI Budgets | Reuses the blind close's code; no route, setting or status field; no CSS. |
| VII UI | One warning Note in the existing jog panel. |
| VIII Security | Same authenticated `/api/roof/jog` route and command header. |
| IX Docs | User guide (roof motion, calibration), developer notes in the code; constitution amended. |
| X Quality | Quality gate. |

## Project Structure

- lib/RoofLogic: `RoofController.h`, `RoofController.cpp`, `RoofControllerCalibrate.cpp`, `RoofControllerBlind.cpp`.
- lib/CalibrationLogic: `StartInputs::jog`, `attendedRefusal`, `Teach::jog`.
- src/RoofTeach.cpp: a warning in the log when a blind jog starts.
- web: `moveGate` (Calibrate.tsx), `JogPanel` (TeachCard.tsx), the wizard's motor screens, i18n.
- test: `test/test_roof_jog_blind`, `test/test_calibration_teach`, Vitest TeachCard and WizardMotor.

## Complexity Tracking

None outside the constitution amendment.
