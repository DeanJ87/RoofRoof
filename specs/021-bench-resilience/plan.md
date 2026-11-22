# Implementation Plan: Bench Resilience

**Branch**: `spec/021-bench-resilience` | **Date**: 2026-11-22 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/SafetyInputLogic` (`ParkRule`): a `NinaMemory` (the last good N.I.N.A. reading and the
  address generation it came from, `rememberNina`) and `closeHold`, which says whether a move under
  way may go on: parked on the current reading, or riding through a failed read on the remembered
  one, and the time until which that reading is fresh.
- `lib/RoofIoLogic` (`RoofGates`): `GateInputs` gains `ninaGrace`, `parkFreshUntilMs` and `nowMs`.
  A new `closeMayContinue` (the park result or the ride-through, every fitted switch closed now,
  and `nowMs <= parkFreshUntilMs`) replaces `parkHolds` in `interlockLost` and in the calibration
  run's open-leg backstop. `gateCommand` (the start) still uses `parkHolds`. `interlockBits` gains
  `InterlockNinaStale`.
- Firmware: `SafetyInputs` keeps the memory and the hold, logs a ride-through, and the main loop
  hands the hold over with the park result; `RoofControl::currentGates` stamps `nowMs` with the
  poller's clock.
- `lib/ConfigModel` (`ConfigSave`): `readBase` (the body's `baseRevision`, `baseValues`) and
  `staleFields` / `checkSave` (field-level compare of the loaded values with the settings as
  `GET /api/config` shows them).
- Firmware: `GET /api/config` sends `X-Config-Revision`; the save runs its stale check and the
  write under the settings save lock (a new `UpdateConfigCallback` in `main.cpp`, which also bumps
  the revision inside the lock); 409 with `conflicts` and `revision`; 200 with `revision`; the
  revision starts random.
- Web: `lib/configPatch.ts` (diff, credential partners, base values, rebase); `useConfigEditor`
  sends the patch with its base, keeps the raw loaded document and revision, and exposes `rebase`;
  Settings, the wizard and the calibration page handle the new `conflict` outcome. i18n, Vitest,
  mocks, Playwright.
- Constitution 2.1.1 (Principle II clarification), roadmap entry, OpenAPI, docs, CHANGELOG.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the save
contract in [contracts/config-save.md](contracts/config-save.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 004's park rule and poller; spec 003's gates and interlock; spec 016/019's calibration and jog; spec 009/015's settings editor; spec 020's SiteSync and `keepFollowed` |
| **Storage** | none new; the revision lives in RAM |
| **Testing** | `pio test -e native` (new `test_park_grace`, `test_config_save`; `test_roof_gates` extended); Vitest (`configPatch`, `useConfigEditor`, Settings); Playwright (settings) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | the hold is O(1) per evaluation; the stale check runs only on a save with an old revision |
| **Constraints** | flash near its warning line: keep additions small; the check's JSON lives on the heap for one save |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | The interlock still stops, drops EN and latches; its decision is pure; the control task applies the freshness bound itself | `test_roof_gates`, `test_park_grace` |
| II. Parked mount | Start unchanged (`gateCommand` uses `parkHolds`). During a move: any good reading that isn't parked, a disagreement, a switch open or unreadable, a stale hand-over, N.I.N.A. not configured or re-addressed stop at once; a missed reply rides only on a remembered good, parked reading, and only until it is 10 s old, checked every step. Wording clarified (2.1.1, PATCH): the re-check uses the latest good reading while fresh | `test_park_grace` (each transition, the 10 000/10 001 ms boundary, a timeline), `test_roof_gates`, `test_park_logic` unchanged |
| III. Unsafe ends closed | No reading is used past its freshness; the weather path is untouched | `test_roof_gates` |
| IV. Alpaca | Alpaca's close goes through the unchanged start gate | `test_dome_close` unchanged |
| V. Pure logic | Hold, memory, gate rule, save check in `lib/`, tests first | native suites |
| VI. Budgets | Two scalars and a reading copy on the loop; the save check's document on the heap, only on a stale save | build, `flash_budget.py`, `size_check.py` |
| VII. UI | A toast with Reload, the wizard's existing message line; no new components | Vitest, Playwright |
| VIII. Security | `POST /api/config` still calls `requireAuth`; secrets compare masked; credential partners checked together | `test_config_save`, Vitest |
| IX. Docs | REST (header, 409, partial save), safety inputs and close pages, configuration page; OpenAPI regenerated | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py, function limits | check.py |
| XI. Heritage | `WebServerApi.cpp`'s save handler diverges from SQMeter (recorded) | - |

**Gate**: pass, no exceptions. Principle II is clarified, not relaxed (spec, assumption 6).

## Project Structure

```text
specs/021-bench-resilience/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
.specify/memory/constitution.md (2.1.1), specs/ROADMAP.md, CHANGELOG.md
lib/SafetyInputLogic/ include/ParkRule.h, src/ParkHold.cpp (new)
lib/RoofIoLogic/ include/RoofGates.h, src/RoofGates.cpp, src/RoofText.cpp
lib/CalibrationLogic/src/CalibrationRunLegs.cpp (backstop)
lib/ConfigModel/ include/ConfigSave.h, src/ConfigSave.cpp (new)
include/{SafetyInputs,RoofControl,WebServer}.h; src/{SafetyInputs,RoofControl,main,WebServer,WebServerApi,WebServerBackup}.cpp
test/test_park_grace, test/test_config_save (new); test/test_roof_gates
web/src: lib/configPatch.ts (new), lib/settingsApi.ts, hooks/useConfigEditor.ts, components/{Settings,Wizard,Calibrate}.tsx, mocks, i18n
tools/api/operations.json, docs/api/openapi.json, docs/api/rest.md, docs/user-guide/{safety-inputs,close-on-unsafe,configuration}.md
```

**Structure Decision**: the ride-through is decided where the park result is (the loop's safety
evaluation, which sees every reading) and bounded where the motor is (the control task's step), so
neither the loop's rate nor the poller's can stretch it. The stale check is a pure function over
two JSON documents, run inside the one save lock every writer already takes.

## Phases

1. **Pure logic, tests first**: `NinaMemory`, `closeHold`; gates; save check.
2. **Firmware glue**: hand-over, clock, log; the save under the lock; header; revision seed.
3. **Web**: patch, base, conflict outcome, rebase; Settings, wizard, calibration page.
4. **Constitution, roadmap, docs, OpenAPI**.
5. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
