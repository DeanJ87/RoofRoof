# Implementation Plan: A Cohesive Setup Journey

**Branch**: `spec/026-cohesive-journey` | **Spec**: [spec.md](spec.md) | **Audit**: [journey-audit.md](journey-audit.md)

## Summary

One shared explanation of every refusal or hold (`lib/moveBlockers.ts` and `MoveBlockers`) on the
dashboard, Calibrate and the wizard's motor screens; an attended **Release hold** for the automatic
close's stand-down, decided in `lib/CloseSequenceLogic`, with its countdown in the status; the
roof's setup stages on the dashboard and in Settings, Roof; Settings, Roof in setup order with the
move time limits beside travel and speed; `roof.measured`; typing in a followed SQMeter group sets
it here, on the page and on the device; the test box's weather bypass and source in one save. The
built page leaves the English-only device catalogue out, which pays for all of it.

## Technical Context

C++17 firmware (lib pure, Unity tests), Preact + TypeScript UI (Vitest, Playwright against the
WASM demo core), as in spec 001. Budgets: LittleFS at its limit before this work, flash at 92.3%.

## Constitution Check

| Principle | How it is kept |
|---|---|
| I Motion Safety | No motion rule changes. Release never moves the roof. Jog and calibration gates unchanged. |
| II Parked mount | Unchanged; the park blocker only explains the gate. |
| III Unsafe ends closed | The release is refused while anything wants the roof closed and is re-checked by the sequence's own step; it only re-arms the close (the transition the re-arm spell makes). Native tests: release while safe, ignored while unsafe or in daylight, a new Stop wins, halted released, cannot-move not. |
| IV Alpaca | Not touched. |
| V Testable logic | Release, countdown, `measured`, unfollow rule in lib with native tests; page logic in pure modules with Vitest; journeys in Playwright. |
| VI Budgets | UI 2 KB gzipped smaller than main, LittleFS within its 75% limit; flash +3.7 KB (92.8%), recorded. |
| VII UI | DS components only; every greyed button points at its reasons; bypassed checks named as bypassed. |
| VIII Security | The release route needs login and the command header, like every roof command. |
| IX Docs | User guide (dashboard, configuration, close-on-unsafe, roof-motion, test-box, calibration-run, setup-wizard), REST, OpenAPI, schemas, DIA-22 updated and re-confirmed. |
| X Quality | Quality gate run. |

No constitution amendment: no principle changes.

## Project Structure

- lib: `CloseSequenceLogic` (release, countdown, refusal text), `RoofIoLogic` (status field),
  `ConfigModel` (`measured`, unfollow rule), `SettingsDeps` catalogue.
- src: `CloseOnUnsafe` (request flag, countdown), `WebServerRoof` (route), `WebServerTeach`, `main`.
- tools/demo-core: the same in the demo.
- web: `lib/moveBlockers.ts`, `lib/setupProgress.ts`, `lib/moveLimits.ts`, `components/MoveBlockers.tsx`,
  `components/SetupProgress.tsx`, Calibrate, RoofCard, wizard MotorPages, RoofTab, TestBox,
  FollowsSqmeter, syncView, configPatch, `vite.i18n.ts`.

## Pull requests

One PR: the parts share Calibrate, Settings and the string budget, and the build saving pays for
them together, so splitting would leave an intermediate PR over the LittleFS limit.

## Complexity Tracking

None.
