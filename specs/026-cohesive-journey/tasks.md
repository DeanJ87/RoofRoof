# Tasks: A Cohesive Setup Journey

**Input**: spec.md, plan.md, research.md, data-model.md, contracts/release-hold.md, journey-audit.md

**Tests**: required (constitution V).

## Phase 1: Audit and setup

- [x] T001 Walk the demo as a new owner at 1280 and 390 px; write `journey-audit.md` (map, 40 moments, IA proposal)
- [x] T002 Measure the budgets (LittleFS at 75%, flash 92.3%) in research.md R3

## Phase 2: Foundational

- [x] T003 Leave the unnamed `device.*` catalogue out of the build (`web/vite.i18n.ts`, both vite configs)
- [x] T004 [P] Native tests first, then `Inputs::releaseHold`, `standDownLeftMs`, `releaseRefusal` in `lib/CloseSequenceLogic`
- [x] T005 [P] `Snapshot::closeRearmSeconds` and `writeCloseRearm` in `lib/RoofIoLogic`; status schema; REST docs

## Phase 3: US1 why can't it move (P1)

- [x] T006 `web/src/lib/moveBlockers.ts` with Vitest `moveBlockers.test.ts`
- [x] T007 `MoveBlockers` component; Calibrate lists it once; wizard motor screens; TeachCard
- [x] T008 Dashboard Roof card: the list under the buttons, the hold under Automatic close, the fault's "?"
- [x] T009 `POST /api/roof/release-hold` (firmware, demo core, demo handler), routes, operations, OpenAPI, examples
- [x] T010 Remove the checklist strings and the copy exceptions they needed

## Phase 4: US5 test box (P1)

- [x] T011 Weather bypass turns the SQMeter source off in the same save; the "no weather source" warning in Settings; Vitest

## Phase 5: US4 follow SQMeter (P1)

- [x] T012 Native tests first, then `unfollowEdited` in ConfigModel
- [x] T013 `editChanges`, `SYNC_GROUP_FIELDS` in `withSentTogether`; fields no longer read-only; FollowsSqmeter notes; Vitest

## Phase 6: US2 and US3 setup progress and settings (P1, P2)

- [x] T014 Native tests first, then `roof.measured` (ConfigModel, teach confirm, demo core), schemas, fixtures, catalogue
- [x] T015 `lib/moveLimits.ts` (pinned to the device's 65.9 s)
- [x] T016 `lib/setupProgress.ts`, `SetupProgress`, the dashboard card, Settings, Roof order and the Travel and time limits card
- [x] T017 Calibration accept and the wizard's direction answer send the measured bits

## Phase 7: Verify

- [x] T018 Playwright `web/tests/journey.spec.ts`: stages, held and released, no release while unsafe, each refusal with its fix, a timeout fault, the test box save, typing over SQMeter
- [x] T019 Docs: dashboard, configuration, close-on-unsafe (Release hold), roof-motion, test-box, calibration-run, setup-wizard, commissioning; DIA-22 updated, other diagrams re-confirmed
- [x] T020 Full checks per spec 001 quickstart §1; before and after screenshots
- [x] T021 Review round 1 (code and safety): fixed each button's own reasons, Release hold's Cancel, the weather bypass refused while a source reads unsafe and undone cleanly, `measured` kept on a recalibration and cleared on typing, one follow rule for Settings and the wizard (the Pushover sound excluded), the release logged as it happened; tests added
- [x] T022 Review round 2, a first-time owner in the demo: fixed a park blocker reading as if it stopped Open (each one-button reason now names its button), the hold now says it won't close by itself, the timeout's "?" mentions a stuck roof first, the supply's "?" the battery, linked settings cards clear the sticky tabs, the direction stage says where it is checked; the rest recorded under "Left for the owner"
- [ ] T023 Code review round 2 on the changes since round 1

## Converge

Run after T021.
