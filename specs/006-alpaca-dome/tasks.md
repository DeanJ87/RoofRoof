# Tasks: Alpaca Dome

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: the pure logic first (origin, bridge, Dome members), then the firmware glue, the
simulator and ConformU, then web, docs and the full check. Every task ends with `pio test -e native`
green; firmware tasks also with `pio run -e esp32dev`.

## Phase 1: Setup

- [x] T001 Add the new test suites' include paths if needed (`[env:native]` in `platformio.ini`) and `lib/AlpacaLogic` as a dependency of `lib/RoofIoLogic` (library.json)

## Phase 2: Tests first

- [x] T002 [US2] `test/test_roof_logic`: `Origin::Client` refused for a move from unknown (open and close), refused for clearing a fault, accepted from a known position; `check()` agrees with `command()`
- [x] T003 [US1][US3] `test/test_dome_bridge/test_main.cpp`: every row of the shutter state table (7 states × close waiting, not running); `roofCommand` for each action; `countsAsPersonStop` for every command × origin; `domeRefusalText` adds the fault only for `faultLatched`
- [x] T004 [US1][US2][US3] `test/test_alpaca_dome/test_main.cpp` with a stub backend: the configured-devices entry and UniqueID; every `Can*`; NotImplemented members (GET and PUT); `Slaved` get/put true/false/missing; `ShutterStatus`, `Slewing`, `DeviceState` (with and without the clock); interface version 3; open and close need a connection (NotConnected, backend not called); refused → 0x40B with the message; accepted → 0; abort connected or not calls the backend; Alpaca off → disabled error, backend not called, nothing listed; GET of a PUT member → 400; the UI's requests don't count as activity
- [x] T005 [US5] `test/test_alert_clients` (or `test_alpaca_logic`): the client watch with the Dome as device 0: watching, silent after the silence time, disconnected; addClientInputs names the Dome

## Phase 3: Pure logic

- [x] T006 `Roof::Origin::Client` in `RoofTypes.h` and its name, until T002 passes
- [x] T007 `lib/RoofIoLogic/include/DomeBridge.h`, `src/DomeBridge.cpp` until T003 passes
- [x] T008 `lib/AlpacaLogic/include/AlpacaDome.h`, `ALPACA_ERR_INVALID_OPERATION`, `Backend::domeStatus/domeCommand`, `src/AlpacaDome.cpp` and the router dispatch, until T004 passes; update `test_alpaca_logic`'s stub backend
- [x] T009 Deliberate-breakage run (outside the repo): at least 8 breakages of the Dome rules (connection check, disabled check, refusal error, shutter mapping, client origin, stop counting), each must make a test fail; record the result in the PR

## Phase 4: Firmware

- [x] T010 `src/RoofControl.cpp`: count a stop or abort with `RoofIo::countsAsPersonStop` (person or client)
- [x] T011 `include/WebServer.h`, `src/WebServerAlpaca.cpp`: list the Dome; `domeStatus` from the roof snapshot through `DomeBridge`; `domeCommand` through `RoofControl::command(..., Origin::Client)`, with the fault named on a fault refusal; log each command with the client ID and outcome (FR-010)
- [x] T012 `tools/demo-core/emulated_device.h`: the new backend members (no device); rebuild the WASM core

## Phase 5: Simulator and ConformU

- [x] T013 `tools/alpaca-sim/main.cpp`, `build.sh`: the simulated Dome (controller + `RoofSim` plant stepped on simulated time, gates from `unsafe`/`unparked` options, the bridge)
- [x] T014 ConformU conformance and alpacaprotocol locally against the simulator: zero errors, issues and configuration alerts; fix what it finds (with tests)
- [x] T015 `.github/workflows/alpaca-conformance.yml` (SQMeter's shape, Dome URL, path filters for Alpaca, roof logic, simulator)
- [x] T016 Spec 001's Complexity Tracking exception marked resolved by 006

## Phase 6: Web

- [x] T017 Mocks (`mocks/data.ts`, `mocks/handlers.ts`) list the Dome with devicestate and a client entry; tests and Playwright specs updated (Alpaca page lists the Dome; dashboard imaging-app tile); the "no device" texts and their context notes describe the demo; the Alpaca setting's hint says a client can open and close the roof within the safety rules
- [x] T018 `npm run typecheck`, `npm test`, `npm run build`, Playwright; `node tools/i18n/check.mjs`, `literals.mjs`

## Phase 7: Docs and registries

- [x] T019 `docs/user-guide/alpaca.md`: the Dome, members, refusals, stop rule, N.I.N.A. setup, ConformU only in bench mode or attended; DIA-18 redrawn with its sources and fingerprint
- [x] T020 `tools/api/routes.json` reason for `/api/v1`; `tools/api/openapi.py` intro; regenerate `docs/api/openapi.json`; `docs/api/integrations.md` / `rest.md` where they mention Alpaca
- [x] T021 README, `docs/index.md`, `docs/llms.txt`, `docs/development/workflow.md`, `docs/development/heritage.md` (AlpacaRouter diverged), `CHANGELOG.md`

## Phase 8: Check and review

- [x] T022 Full local check (spec 001 quickstart §1), flash budget, docs build
- [x] T023 Independent safety reviews until one reports safe to merge; fixes with tests
- [x] T024 Converge

## Phase 9: Convergence

- [x] T025 List `test_dome_close` with the new native suites in `quickstart.md` and `docs/development/heritage.md` per plan: Project Structure (partial)
