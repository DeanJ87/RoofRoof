# Implementation Plan: Demo and Bench Mode

**Branch**: `spec/011-demo-bench` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/RoofPlantLogic` (new, pure): the simulated roof, moved out of `test/roof_sim` and set up
  from the roof settings (`RoofPlant::Plant`, `RoofPlant::forSettings`), with supply sag; and the
  bench supply guard (`RoofPlant::BenchSupplyGuard`). The native tests and the Alpaca simulator
  keep using it through `test/roof_sim/RoofSim.h`.
- `lib/DriverLogic`: `Driver::physicalOutputs(logical, bench)`, the one decision of what reaches
  the pins; all off in a bench build.
- `lib/FirmwareImage`: the marker's build field becomes `standard` or `bench`; `check()` takes the
  running build and refuses a bench image on a standard board (`Verdict::BenchImage`).
- `lib/RoofIoLogic`: `FaultCause::BenchSupply`; the status's roof section gains `simulation`.
- `lib/AlpacaLogic`: `domeDeviceInfo(simulation)` for the bench and demo descriptions.
- Firmware (`[env:esp32dev_bench]`, `-DROOFROOF_BENCH=1`; `include/BenchMode.h`):
  - `MotorDriver`: pins only through `physicalOutputs`; in bench, no PWM attached, the logical
    state kept for the simulated roof and the watchdog;
  - `RoofControl`: in bench, the simulated roof's readings replace the ADC's under the roof's lock,
    the guard checks the real supply input (1 s at start-up before the task starts, then every
    step) and latches `benchSupply`;
  - `FirmwareMarker` writes `build=bench`; the web upload checks with the running build; command-
    line OTA checks the received image's marker before it is booted (`OtaImageCheck`);
  - alerts get "Bench: " in a bench build (`AlertDispatcher`); the Dome's description says bench;
    the log says bench mode at start-up.
- Demo core: `tools/demo-core/emulated_roof.{h,cpp}`: the controller, the simulated roof, the
  gates, `checkCommand`, the close sequence, simulated SQMeter and N.I.N.A., the injectors, the
  roof alerts; `EmulatedDevice` lists the Dome and adds the roof to its status and alerts; new
  bindings for the roof commands, the injectors and the panel's view.
- Web: demo handlers for `/api/roof/*`; the Demo panel's Roof section; a bench banner on every page
  (`BenchBanner`, from `status.roof.simulation`); after 009: the demo wired to 009's roof card,
  the tour's roof steps.
- CI: the bench image is built (not released). Docs: `docs/user-guide/bench-mode.md`, the demo page,
  the API/WebSocket field, heritage, changelog.

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (firmware, native tests, Emscripten), TypeScript strict (Preact) |
| **Primary Dependencies** | specs 002-007's libraries; the demo core (Emscripten, `tools/demo-core/VERSION`); MSW |
| **Storage** | demo: sessionStorage (roof position, latched fault, injectors); firmware: none new (the fault store is 003's) |
| **Testing** | `pio test -e native` (new `test_roof_plant`, `test_bench_mode`; extended `test_driver_logic`, `test_firmware_image`, `test_roof_status`, `test_alpaca_dome`); Vitest (`demo/__tests__/roof.test.ts`, contracts); Playwright demo spec; bench checks |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB); browsers for the demo |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | demo: a 10 s clock jump is 500 roof steps, well under a millisecond each |
| **Constraints** | standard build grows < 4 KB; bench code compiled only into the bench build; no lib/ change may make the standard build depend on the plant |
| **Scale/Scope** | ~250 lines of lib, ~400 of tests, ~450 of demo core, ~350 of web, ~150 of firmware glue |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Bench mode never drives the motor: one pure decision (`physicalOutputs`) holds every pin low in a bench build, and PWM is never attached. The watchdog, step gap and fault latch run unchanged. The standard build has no bench code. The demo runs the same motion logic, never on hardware. | `test_driver_logic` (every command and duty), `test_bench_mode` |
| II. Parked mount | Unchanged logic, exercised in the demo (park fails, unparked close refused). A bench image on a powered roof can't report a closed roof: the supply guard latches a fault first. | `test_bench_mode` (guard), demo roof tests |
| III. Unsafe ends closed | The demo's unsafe and rain injectors go through the real verdict and close sequence. A bench image can't reach a roof by the upload paths (marker refused), so it can't falsely report a close. | `test_firmware_image`, demo roof tests |
| IV. Alpaca | The demo's Dome is the firmware's router and Dome mapping. The bench board runs the real Alpaca stack, so ConformU can run against real hardware with no motor, as the constitution asks. UniqueIDs unchanged. | `test_alpaca_dome`, ConformU (CI against the simulator) |
| V. Pure logic | Plant, guard, pin decision, marker rule, status field: all pure with native tests. | as above |
| VI. Budgets | Standard build: the marker rule, the OTA check and one status field (< 4 KB). Bench build checked to fit. The demo core's size is measured. | build, `flash_budget.py`, `size_check.py` |
| VII. UI | The bench banner uses `Note` (no new styles); the Demo panel's Roof section uses `Button`, `card-group`; strings in i18n. 009's roof card is reused, not copied. | Vitest, Playwright a11y |
| VIII. Security | No new device endpoint. The demo's roof commands are as the device's (auth is not modelled in the demo). | - |
| IX. Docs | Bench-mode page, demo page, status field, wiring link, heritage, changelog. | `mkdocs build --strict` |
| X. Quality | check.py; functions ≤ 60 lines, complexity ≤ 15. | check.py |
| XI. Heritage | The demo's roof is new to RoofRoof (SQMeter has no roof); `FirmwareImage` diverges (build kinds), recorded. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
lib/RoofPlantLogic/include/RoofPlant.h, BenchGuard.h     new
lib/RoofPlantLogic/src/RoofPlant.cpp, BenchGuard.cpp     new
lib/DriverLogic/…                                         physicalOutputs
lib/FirmwareImage/…                                       build kinds, BenchImage verdict
lib/RoofIoLogic/…                                         FaultCause::BenchSupply, Snapshot::simulation
lib/AlpacaLogic/…                                         domeDeviceInfo(simulation)
include/BenchMode.h, include/OtaImageCheck.h              new
src/MotorDriver.cpp, RoofControl*.cpp, FirmwareMarker.cpp,
  WebServerUpdates.cpp, WebServerAlpaca.cpp, AlertDispatcher.cpp, main.cpp, OtaImageCheck.cpp
test/roof_sim/RoofSim.h                                   now wraps lib/RoofPlantLogic
test/test_roof_plant, test/test_bench_mode                new
tools/demo-core/emulated_roof.{h,cpp}, bridge.cpp, build.sh
tools/alpaca-sim/build.sh                                 adds lib/RoofPlantLogic
web/src/demo/{device.ts,handlers.ts,coreApi.ts,roof.ts,panel/RoofControls.tsx,tour/steps.ts}
web/src/components/BenchBanner.tsx
platformio.ini ([env:esp32dev_bench]), .github/workflows/build.yml (bench job)
docs/user-guide/bench-mode.md, docs/live-demo.md, docs/api/*, heritage, CHANGELOG
```

## Phases

1. Plant and guard in lib, tests moved over (no behaviour change for existing tests).
2. Pin decision, marker rule, fault cause, status field, Dome description (lib + tests).
3. Firmware bench glue and the standard build's OTA check; both builds.
4. Demo core roof; bindings; demo handlers and panel; contracts.
5. Docs, CI.
6. After 008 and 009 merge: rebase, the roof card in the demo, the tour, contracts regenerated.
