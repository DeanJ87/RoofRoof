# Tasks: Setup Wizard Redesign

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/ui-components.md](contracts/ui-components.md)

Tests first where the logic is new (components, screen list, selection rule).

## Phase 1: Setup

- [x] T001 Run `npm ci` in `web/`, build and record the current UI size (123.3 KB JS + CSS gz)

## Phase 2: Foundational (the design system's onboarding set)

- [x] T002 [P] Vitest for `Modal` (role, aria-modal, label, Tab/Shift+Tab wrap including the stop
  bar slot, Escape calls `onEscape`, focus restored on unmount, `#main` fallback) in
  `web/src/components/__tests__/Onboarding.test.tsx`
- [x] T003 [P] Vitest for `ChoiceGroup` (radiogroup labelled, radios named by title, none checked
  for `null`, change calls `onChange`, badge shown) and `ProgressMeter`'s `valueText`, same file
- [x] T004 Add `Modal`, `ChoiceGroup`, `SpeechBubble`, `Mascot`, `ProgressMeter.valueText` and the
  new `Icon` paths to `web/src/components/ui.tsx`
- [x] T005 Add the tokens, `color-scheme: dark`, and the modal, choice, bubble and mascot styles
  (blur with fallback, reduced motion, phone layout, Stop bar clearance) to `web/src/index.css`;
  remove the page-list heading styles spec 015 used
- [x] T006 Amend constitution VII (2.1.0, sync report) in `.specify/memory/constitution.md` and add
  DS-11 to DS-13 and DS-29 to `docs/development/coding-standards.md`; record the `ui.tsx` change in
  `docs/development/heritage.md`

## Phase 3: User Story 1 - one question at a time (P1)

- [x] T007 [US1] Export field groups without their cards: `TimeZoneFields`, `NtpFields`
  (`settings/TimeTab.tsx`), `CurrentFields`, `HallFields` (`settings/RoofTab.tsx`), the channel
  groups (`settings/AlertChannelsCard.tsx`); Settings renders as before
- [x] T008 [US1] Vitest for the screen list: every screen has a question and a why, the conditional
  screens (Hall thresholds, weather address, alert details) follow their settings, the answered
  rules, the selection rule, `?step=` and `?screen=` start points (`web/src/components/wizard/__tests__/screens.test.ts`)
- [x] T009 [US1] Write the screen list and bodies in `web/src/components/wizard/screens.tsx` and
  `ChoiceScreens.tsx`, reusing spec 015's `SupplyCheck`, `TestPanel`, `ReviewPage` checks; remove
  the old page components that are no longer used
- [x] T010 [US1] Rewrite `web/src/components/Wizard.tsx` as the dialog: header (Back, progress,
  Close), speech bubble, body, why panel, footer (Skip for now, Continue with its reason), spec
  015's save/step/discard rules per screen, done screen
- [x] T011 [US1] `/wizard` renders the dashboard with the wizard as a modal; `Layout` makes header,
  skip link and main inert and leaves the Stop bar to the modal (`web/src/App.tsx`,
  `web/src/components/Layout.tsx`)
- [x] T012 [US1] i18n: questions, whys, choices, labels in `web/src/i18n/en.json` with
  `en.context.json` notes; remove the spec 015 strings no longer used
- [x] T013 [US1] Rewrite `web/src/components/__tests__/Wizard.test.tsx` for every spec 015
  guarantee on the new screens (resume, `?step=`, password offer and warning, saves with the step,
  refused save keeps the screen, discard on Back and Skip, Test freshness, Finish, skip warning,
  no roof command on any screen)

## Phase 4: User Story 2 - leave, resume, skip, run again (P1)

- [x] T014 [US2] The "Leave setup?" screen (Keep going first, Leave for now back to the opener's
  page, Skip setup with spec 015's warning); Escape and Close open it; Escape on it keeps going
- [x] T015 [US2] Vitest: Escape and Close never leave directly; Leave for now routes back and
  saves nothing; Skip setup sets done after the warning

## Phase 5: User Story 3 - Stop within reach (P1)

- [x] T016 [US3] Playwright: with the roof moving, Stop is visible, topmost at its centre, inside
  the dialog, reached by Tab, and stops the roof; a save is refused with the reason
  (`web/tests/wizard.spec.ts`)

## Phase 6: User Story 4 - keyboard, screen reader, phone (P2)

- [x] T017 [US4] Playwright `web/tests/wizard-a11y.spec.ts`: axe (WCAG 2.2 AA tags) and reflow on
  every screen at 320 and 1280 px, light and dark; arrow keys in a choice group; focus on the
  question after each change
- [x] T018 [US4] Replace the 11 wizard entries in `web/tests/a11y/inventory.ts` (now covered by
  `wizard-a11y.spec.ts`); keep `a11y/baseline.json` free of wizard entries

## Phase 7: User Story 5 and polish

- [x] T019 [US5] Keep `SetupPrompt` and the dashboard banner tests green (`tests/dashboard.spec.ts`)
- [x] T020 Rewrite `web/tests/wizard.spec.ts` flow for the screens (walk, resume, skip, run again,
  no roof command); update the demo tour if it names the wizard
- [x] T021 Screenshots: `web/tests/screenshots.spec.ts` wizard shots (choice screen, fields screen
  with readings, review, done; phone and desktop) into `docs/assets/screenshots/`; the PR's light
  and dark, phone and desktop set
- [x] T022 Docs: `docs/user-guide/setup-wizard.md`, `docs/getting-started/first-setup.md`,
  `docs/live-demo.md` if it describes the pages; CHANGELOG; `specs/ROADMAP.md` 018 entry
- [x] T023 Size: build, pack, `size_check.py`; record the baseline with its reason
  (`tools/ui/size-baseline.json`)
- [x] T024 Full local check (quickstart §1 of spec 001), quality gate, i18n checks, docs build
- [x] T025 Independent review (accessibility, focus trap, Stop over the dialog, spec 015 save and
  safety behaviour, DS consistency); fix and repeat until safe to merge
