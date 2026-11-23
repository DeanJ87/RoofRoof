# Tasks: Setup Wizard Journey

**Input**: Design documents from `specs/025-wizard-journey/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/wizard-journey.md

**Tests**: required (constitution V: Vitest for UI logic and components; the bug fix has a failing
test first).

## Phase 1: Setup

- [x] T001 Reproduce each reported problem in the demo and record the root causes in `specs/025-wizard-journey/research.md` (R1, R2)
- [x] T002 Measure the UI size baseline with `tools/ui/size_check.py` and record it in research.md (R6)

## Phase 2: Foundational (shared components)

- [x] T003 [P] Add the checkbox form of the choice card group (`multiple`, `values`) in `web/src/components/ui.tsx` and its mark in `web/src/index.css`; test in `web/src/components/__tests__/Onboarding.test.tsx`
- [x] T004 [P] Add the plain (card-less) form of `SettingsCard` in `web/src/components/settings/controls.tsx`
- [x] T005 Export the calibration run's `Checks`, `StartForm`, `Running`, `Result`, `isFresh` and a shared `acceptRoof` from `web/src/components/Calibrate.tsx`, each with the plain form
- [x] T006 Export `JogControls` and give `TeachCard` a plain form in `web/src/components/calibrate/TeachCard.tsx`
- [x] T007 [P] Let the alert channel groups hide their own switch in `web/src/components/settings/AlertChannelsCard.tsx`; export `MqttFields` from `web/src/components/settings/NetworkTab.tsx`
- [x] T008 [P] Hide the wiring check's Calibrate link inside the wizard in `web/src/components/pins/WiringCheck.tsx`

## Phase 3: User Story 3 - progress is reliable (P1)

- [x] T009 [US3] Write the failing tests first: "No password" kept after a remount; resume at `setup.step` in every state; an explicit start applied once; Skip for now records the part, in `web/src/components/__tests__/WizardResume.test.tsx`
- [x] T010 [US3] Redraw the twelve parts in `web/src/components/wizard/pages.ts` and the screen list in `web/src/components/wizard/screens.ts`; update `web/src/components/wizard/__tests__/screens.test.ts`
- [x] T011 [US3] Remove page-only state and the password auto-offer from `web/src/components/wizard/context.ts`, `DevicePages.tsx` and `Wizard.tsx`; resume in every state; consume the explicit start; Skip for now records the part

## Phase 4: User Story 1 - the motor inside the wizard (P1)

- [x] T012 [US1] Add the direction, teach and calibration screens and the test box note in `web/src/components/wizard/MotorPages.tsx`
- [x] T013 [US1] Move the typed Hall and motion values behind "Enter manually instead" (the `motion` screen) in `web/src/components/wizard/HardwarePages.tsx`; remove the Hall fitted, Hall, supply, teach-link and calibration-link screens
- [x] T014 [US1] Vitest for the motor screens (gates, attended, test box note, direction "No", manual) in `web/src/components/__tests__/WizardMotor.test.tsx`

## Phase 5: User Story 2 - never sends you out (P1)

- [x] T015 [US2] `leaveTo` (save or record the part, then route) and fixes in place in `web/src/components/Wizard.tsx`
- [x] T016 [US2] Custom pins inline on the board screen; current sense on the wiring screen in `web/src/components/wizard/BoardPages.tsx`
- [x] T017 [US2] Vitest: no wizard control routes away except `leaveTo`, in `web/src/components/__tests__/WizardBoard.test.tsx` and `WizardFlows.test.tsx`

## Phase 6: User Story 4 - alerts (P2)

- [x] T018 [US4] The multiple-choice alerts screen with inline channel fields and MQTT's broker in `web/src/components/wizard/SourcePages.tsx`
- [x] T019 [US4] The refused-save note names the fields in `web/src/components/Wizard.tsx`
- [x] T020 [US4] Vitest for the alerts screen (several, none, untick, Pushover errors at their fields)

## Phase 7: User Story 5 - ask once, installer's order (P2)

- [x] T021 [US5] The SQMeter screen (address, login, Test, copy, weather from SQMeter, done rows with Edit) and the site and time screen in `web/src/components/wizard/DevicePages.tsx`; N.I.N.A. with Alpaca in `SourcePages.tsx`; weather screens and the weather Test result removed (`TestPanel.tsx`)
- [x] T022 [US5] Review rows mapped to the new parts; the Hall row from the live reading, in `web/src/components/wizard/review.ts`; update `__tests__/review.test.ts`
- [x] T023 [US5] No rain-contact leftovers in the wizard, its strings or its docs

## Phase 8: Polish and cross-cutting

- [x] T024 Strings: remove the old screens' keys, add the new ones with context notes in `web/src/i18n/en.json` and `en.context.json`; `node tools/i18n/check.mjs`, `literals.mjs`, `tools/ui/copy_check.py`
- [x] T025 Playwright: walk both branches end to end with the motor screens and reloads mid-way in `web/tests/wizard.spec.ts`; update `wizard-a11y.spec.ts` and `screenshots.spec.ts`
- [x] T026 Docs: rewrite `docs/user-guide/setup-wizard.md`; update `calibration-run.md`, `test-box.md`, `alerts.md` where they mention the wizard, DS-12 in `docs/development/coding-standards.md`; regenerate the wizard screenshots
- [x] T027 Size: `tools/ui/size_check.py` within SIZE-01 and SIZE-02; update the baseline record
- [x] T028 Full quickstart §1 (firmware build, native tests, web typecheck, tests with coverage, build, Playwright, quality gate, flash budget, docs build)
- [ ] T029 Review rounds (opus) including a walk of the demo and the screenshots until "safe to merge"; record them below

## Dependencies

Phase 2 before Phases 3 to 7; T009 before T011; T010 before T012 to T021; Phase 8 last.

## Review rounds

(recorded as they happen)
