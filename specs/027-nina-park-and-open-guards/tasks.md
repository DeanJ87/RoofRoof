# Tasks: N.I.N.A. Park Before Every Move, and Guards a Person Can Confirm

**Input**: spec.md, plan.md, research.md, data-model.md, contracts/api.md

**Tests**: required (constitution V): native tests first for every lib change.

## Phase 1: Setup

- [x] T001 Measure the budgets (flash, LittleFS, Config.h) on main (research R5)
- [x] T002 Constitution 7.0.0 in `.specify/memory/constitution.md`

## Phase 2: Foundational (lib, tests first)

- [x] T003 [P] `RoofGates`: weather reasons in `GateInputs`, park check for Open, override bits, `overridableOpen`, `overrideStillApplies`; interlock during an open; tests in `test/test_roof_gates`
- [x] T004 [P] `Controller::atEnd` and the Hall rail at-rest clear in `lib/RoofLogic`; tests in `test/test_roof_controller`
- [x] T005 `CommandCheck`: already-there first, overrides from a person only, `parkOnly` refusal; tests
- [x] T006 [P] `ParkWait` in `lib/RoofIoLogic`; tests in `test/test_park_wait`
- [x] T007 [P] `DomeBridge` wait states; Dome name, driver info, description in `lib/AlpacaLogic`; tests
- [x] T008 [P] N.I.N.A. https in `ConfigValidate`; web schema; `config-ranges.json`
- [x] T009 [P] `nina_lost` alert event (AlertLogic, DeviceCore, ConfigModel, template variables); tests
- [x] T010 [P] Status writers: `parkWait`, `override`, `openOverridable`, `nina`, `sqmeter` in `RoofStatus`

## Phase 3: US1 park before every move (P1)

- [x] T011 RoofControl owns the wait and the override; main sends the park request and the alert
- [x] T012 Alpaca and MQTT commands go through the same path; Alpaca status reports the wait
- [x] T013 Demo core: the same wait, override and N.I.N.A. answers; rebuild

## Phase 4: US2 and US3 confirmed open and close (P1)

- [x] T014 `POST /api/roof/open|close` take `override`; CloseOnUnsafe masks overridden reasons
- [x] T015 Web: RoofCard buttons (Stop only while moving or waiting), `MoveDialog` listing every reason with the override; Vitest; Playwright

## Phase 5: US4 N.I.N.A. (P2)

- [x] T016 `POST /api/nina/test`; routes, operations, OpenAPI, contract schemas
- [x] T017 Settings N.I.N.A. card: address, Test, park requests together; https accepted

## Phase 6: US5 and US6 (P2)

- [x] T018 SQMeter and N.I.N.A. health on the dashboard and Diagnostics
- [x] T019 Held close explanation; Hall rail fault explanation
- [x] T020 Version with the commit (`scripts/version.py`)

## Phase 7: US7 Home Assistant (P3)

- [x] T021 New entities; Alerts switch always; tests; `docs/integration/home-assistant.md`

## Phase 8: Verify

- [x] T022 Docs: user guide, REST, MQTT, configuration, Alpaca, CHANGELOG; diagrams re-confirmed
- [x] T023 Quickstart §1 of spec 001: firmware, native, web, Playwright, quality gate, flash, docs
- [x] T024 Converge

## Phase 9: Convergence

- [x] T025 Races from the field: the park wait as an explicit phase (`ParkWaitPhase`), the same command joins a wait, the automatic close calls off a waiting Open, a waiting Open is called off by what refuses an open (upload, hotspot, restart); the state machine and the race criteria in spec.md; native tests in `test_move_guard`
- [x] T026 Alpaca after a failed wait: the description leads with "Last move not run: <why>"; ShutterStatus reads the wait only at rest (`test_dome_bridge`)
- [x] T027 Hall rail at boot: a rail at rest in the first 500 ms isn't judged; the at-rest flag is kept in the fault record over a restart (`test_roof_hall_rest`, `test_roof_io`); constitution 7.0.0 text
- [x] T028 Constitution III "shown on every interface": the override on the Alpaca description ("CHECKS OVERRIDDEN:") and a Home Assistant "Checks overridden" entity
- [x] T029 Flash: the SQMeter and N.I.N.A. reads share one job launcher (`siteJobAllowed`, `launchSiteJob`)
