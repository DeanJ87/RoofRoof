# Tasks: Demo and Bench Mode

Tests first for every lib change (constitution V). [P] = can run in parallel.

## Phase 1: The simulated roof in lib (FR-001 to FR-003)

- [X] T001 Create `lib/RoofPlantLogic` with `RoofPlant::Plant` moved from `test/roof_sim/RoofSim.h` (same behaviour, defaults unchanged), plus `sagVolts` and `forSettings()`
- [X] T002 `test/roof_sim/RoofSim.h`: `RoofTest::RoofSim` becomes an alias of `RoofPlant::Plant`; `Bench` unchanged; add the lib to `[env:native]` includes and `tools/alpaca-sim/build.sh`
- [X] T003 `test/test_roof_plant`: forSettings (counts beyond thresholds, polarity, direction, current), sag, jam, stops, a full open and close with `Roof::Controller` for default and odd settings
- [X] T004 `RoofPlant::BenchSupplyGuard` + tests in `test/test_bench_mode` (limit boundary >=, NaN, 1 s confirm, break resets, latched)

## Phase 2: Pure decisions (FR-021, FR-023, FR-024, FR-026)

- [X] T010 [P] `Driver::physicalOutputs` + test over every command and duty (bench all off, standard unchanged)
- [X] T011 [P] `FirmwareImage`: build kinds, `marker(…, build)`, `check(…, runningBuild)`, `Verdict::BenchImage` + tests (the table in contracts/bench-mode.md)
- [X] T012 [P] `RoofIo::FaultCause::BenchSupply` (encode/decode, name, describeFault) + tests
- [X] T013 [P] `Snapshot::simulation` → `roof.simulation` + test
- [X] T014 [P] `Alpaca::domeDeviceInfo(simulation)` + test

## Phase 3: Firmware

- [X] T020 `include/BenchMode.h`; `[env:esp32dev_bench]` in platformio.ini
- [X] T021 `MotorDriver`: physical writes through `physicalOutputs`; bench: no PWM attach, `logicalOutputs()`
- [X] T022 `RoofControl`: bench plant readings and guard (start-up check, per step), `simulation` in the snapshot
- [X] T023 `FirmwareMarker` build field; `WebServerUpdates` checks with the running build
- [X] T024 `OtaImageCheck`: command-line OTA checks the written image before it is booted (both builds)
- [X] T025 Alerts "Bench: " title prefix (bench); Dome description; start-up warning log
- [X] T026 Build both environments with zero warnings; flash budget

## Phase 4: Demo

- [X] T030 `tools/demo-core/emulated_roof.{h,cpp}`: controller, plant, gates, checkCommand, close sequence, simulated SQMeter and N.I.N.A., injectors, alerts, save/load
- [X] T031 `EmulatedDevice`: the Dome, the roof in statusParts, roof alerts, restart, bindings; build.sh sources; rebuild the core
- [X] T032 Web: `coreApi.ts`, `device.ts` (roof methods, defaults), `handlers.ts` (`/api/roof/*`)
- [X] T033 Demo panel Roof section (`panel/RoofControls.tsx`), i18n where the app's strings are used
- [X] T034 `BenchBanner` on every page from `status.roof.simulation`; `types/status.ts`; mocks
- [X] T035 Vitest: the demo roof (open/close timing, each injector, reload as restart), contracts with the roof states and the Dome; regenerate schemas

## Phase 5: Docs and CI

- [X] T040 `docs/user-guide/bench-mode.md`; nav; wiring and OTA pages link it; demo page Roof section; REST/WebSocket `roof.simulation`; OpenAPI regenerated
- [X] T041 heritage, CHANGELOG, README line
- [X] T042 CI: bench build job in build.yml

## Phase 6: After 008 and 009 merge

- [X] T050 Rebase; adapt the demo core to 008's status fields (`applyChecks`, `closePhase`) and command check changes
- [X] T051 Wire the demo to 009's roof card (data it needs comes from the status); bench mode shows through the page-wide banner (no separate card chip)
- [X] T052 Tour roof steps (card, open, unsafe → closed) + tour test; Playwright demo spec
- [X] T053 Regenerate demo core, schemas, OpenAPI, screenshots if affected; re-run every check

## Phase 7: Review and PR

- [X] T060 Independent reviews of bench mode and the plant model until one says safe to merge
- [ ] T061 Quickstart §1 suite; `source_hash.py --check` last; PR
