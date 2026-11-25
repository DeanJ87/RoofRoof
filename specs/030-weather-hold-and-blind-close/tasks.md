# Tasks: Weather Hold, Closing Without a Known Position, and Never Sitting Silent

**Input**: spec.md, plan.md, research.md, data-model.md

**Tests**: required (constitution V): native tests first for every lib change.

## Phase 1: Setup

- [ ] T001 Measure the budgets on main (research R4, R5)
- [ ] T002 LTO for the project's own code (`scripts/lto.py`); measure; check RTC sections
- [ ] T003 `AutoCloseConfig` into `ConfigAutoClose.h`
- [ ] T004 Constitution 8.0.0

## Phase 2: US1 weather hold (P1)

- [ ] T005 [P] `WeatherHold` with tests (blips 2/10/59 s, silent 3/15 s, 61 s, rain, flapping, call-off, close wins, override, hold 0)
- [ ] T006 Wire into `CloseOnUnsafe`; restored sequence starts fired; `autoClose.unsafeHoldSeconds` plumbing; status `roof.weatherHold`
- [ ] T007 [P] Park-source tests for the default rule (nina, switches, ninaAndSwitches) and the "N.I.N.A. unreachable" wording
- [ ] T008 [P] Alpaca OpenShutter refused while unsafe (tests)

## Phase 3: US2 closing without a known position (P1)

- [ ] T009 [P] Controller: automatic Close from an unknown position; W05 test
- [ ] T010 [P] Controller: blind close, stall check, report; tests
- [ ] T011 Close sequence: attempts instead of CannotMove; `BlindClose` action; events to `needs_person`
- [ ] T012 Fault record blind-close flag; RoofControl and RoofFaultStore glue
- [ ] T013 `roof.blindClose`, `roof.blindSagVolts` plumbing; migration on

## Phase 4: US3 and US4 never silent, alerts survive (P1)

- [ ] T014 `needs_person` event end to end (settings, levels, templates, deps)
- [ ] T015 Needs-person watcher (boot, restart not closed, Hall fault, can't move) with tests
- [ ] T016 Alert queue with back-off, supersede, resolve; NVS keep for critical alerts; tests (access point outage story, restart)
- [ ] T017 Status `roof.needsPerson`, MQTT and Home Assistant binary sensor, Banner in the UI

## Phase 5: US5 words, US6 N.I.N.A.-lost close (P2)

- [ ] T018 [P] SQMeter `reasons` text; flag names as words; status and UI
- [ ] T019 [P] `autoClose.ninaLostClose`/`ninaLostMinutes`; override bit; interlock; tests for both states and every origin

## Phase 6: Verify

- [ ] T020 Demo core rebuilt; web settings and notes; i18n
- [ ] T021 Docs: user guide, REST, MQTT, Home Assistant, configuration, CHANGELOG; diagrams re-confirmed
- [ ] T022 Quickstart §1 of spec 001: firmware, native, web, Playwright, quality gate, flash, docs
- [ ] T023 Converge
