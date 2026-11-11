# Tasks: Dashboard and Settings

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/ui.md](contracts/ui.md), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for UI logic and components; the view model's tests are
written before it (TDD).

**Organisation**: the view model and its tests first, then the command hook, the card, the Stop
bar, the Status rows, the Roof tab, then the inventory, i18n, Playwright, docs and the full check.

## Phase 1: Setup

- [ ] T001 `cd web && npm ci`; confirm the base checks pass before changing anything

## Phase 2: Tests first

- [ ] T002 [US1] `web/src/lib/__tests__/roofView.test.ts`: Stop enabled in every combination (state × fault × weather × park × supply × live × hotspot × pending × refusal × in flight); each Open and Close reason in its order (FR-004, FR-005); position text; notes (unknown position, automatic close, close waiting); `stopBarShown`; the Hall scale geometry and clamping
- [ ] T003 [US1] `web/src/hooks/__tests__/useRoofCommand.test.ts`: each answer (200, 202, 409 with `busy`, `restarting`, `hotspot`, 401, 403, no answer) to a command result; every command through `roofCommand` with the header; expiry on state change and after 15 s; the `restarting` hold until a reconnect or 30 s

## Phase 3: View model and hook

- [ ] T004 [US1] `web/src/lib/roofView.ts` until T002 passes
- [ ] T005 [US1] `web/src/hooks/useRoofCommand.ts` until T003 passes

## Phase 4: Roof card (US1, US3)

- [ ] T006 [US3] `web/src/dashboard/HallScale.tsx`
- [ ] T007 [US1][US3] `web/src/dashboard/RoofCard.tsx`: pill, position meter, buttons with reasons, notes, fault and Clear fault, readings, Hall scale
- [ ] T008 [US1] `cards.tsx`, `order.ts`, `Dashboard.tsx`: the Roof card after Status, with the config and live state
- [ ] T009 [US1][US3] `web/src/dashboard/__tests__/RoofCard.test.tsx`: each state renders its pill and buttons; Stop never disabled; disabled buttons described by their reason; the fault and Clear fault; a refusal's text; the readings shown only when fitted or on

## Phase 5: Stop on every page (US2)

- [ ] T010 [US2] `web/src/components/RoofStopBar.tsx` and `Layout.tsx`; `index.css` (`.stop-bar`, `has-stop-bar` lifting the save bar, the demo-panel offset)
- [ ] T011 [US2] `web/src/components/__tests__/RoofStopBar.test.tsx`: shown while opening, closing or a close waits, also when stale; hidden at rest; sends stop

## Phase 6: Status card (US5)

- [ ] T012 [US5] `glance.ts`: `roof-fault`, `roof-close`; `web/src/dashboard/__tests__/glance.test.ts` cases

## Phase 7: Roof tab (US4)

- [ ] T013 [US4] `tabs.ts` (the tab, `roof`/`safety`/`park` error routing and sections), `payload.ts` (carry the three sections when present), `Settings.tsx`
- [ ] T014 [US4] `web/src/components/settings/RoofTab.tsx`: Motion, Hall sensor (with the live scale on the edited thresholds), Current and supply, Weather sources (D-40/D-41 via `DepToggle`), Park (with `park.timeoutSeconds`); the moving and stopped-part-way notes
- [ ] T015 [US4] `web/src/components/__tests__/RoofTab.test.tsx` and `settings/__tests__` payload cases: every field present, a range error routed to the Roof tab, the save payload, the moving note

## Phase 8: Registries, i18n, Playwright

- [ ] T016 `inventory.json`: `status.roof.**` mapped field by field (shown with tests, or not shown with reasons); D-40, D-41 shown on their Settings row; `python3 tools/dashboard/check.py`
- [ ] T017 i18n: every new string in `en.json` with an `en.context.json` note; `node tools/i18n/check.mjs`, `literals.mjs`; copy and label checks
- [ ] T018 Playwright: `dashboard.spec.ts` (the Roof card for the demo's closed roof; inventory ids), `settings-tabs.spec.ts` (Roof tab), `a11y.spec.ts` (dashboard and Roof tab), `layout-overlap.spec.ts` (Roof card buttons at 320/390 px), `screenshots.spec.ts` (Roof card, Roof tab)

## Phase 9: Docs and checks

- [ ] T019 Docs: `docs/user-guide/dashboard.md` (Roof card, Stop bar, Status rows), `configuration.md` (Roof tab), `roof-motion.md`, `docs/hardware/calibration.md` (the live Hall scale); screenshots list
- [ ] T020 Quickstart §1 in full; size check; record numbers for the PR
