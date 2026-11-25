# Tasks: Honest Live State and Consistent Secondary Text

**Input**: Design documents from `/specs/028-live-state-and-consistency/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/live-streams.md

## Format: `[ID] [P?] [Story] Description`

## Phase 1: Setup

- [x] T001 Record the device bundle's size on main (`tools/ui/size_check.py`) as the number not to exceed

## Phase 2: Foundational

- [x] T002 Native tests first: `liveIntervalMs` (rest 2000, moving 500) and the diagnostics stream following it, in `test/test_connection_budget/test_main.cpp` and `test/test_diag_store/test_main.cpp`
- [x] T003 The live schedule in `lib/ConnectionBudget/include/ConnectionBudget.h`; `Diag::streamIntervalMs` uses it (`lib/DiagLogic`)
- [x] T004 Rebuild the demo core (`tools/demo-core/build.sh`)

## Phase 3: User Story 2 - One live rate (P1)

- [x] T005 [US2] `/ws/status` follows the schedule while the roof moves (`src/WebServer.cpp`), keeping the change-driven send and the send on connect
- [x] T006 [P] [US2] `web/src/lib/liveStream.ts`: the same schedule for the demo, with Vitest
- [x] T007 [US2] The demo's two streams use it (`web/src/demo/handlers.ts`)

## Phase 4: User Story 1 - No reading, no state (P1)

- [x] T008 [US1] Vitest first: phase, back-off and quiet helpers in `web/src/lib/__tests__/liveStream.test.ts`; hook behaviour (reading cleared on close, reconnecting, quiet close, wake reconnect) in `web/src/components/__tests__/LiveSocket.test.tsx` (the jsdom project)
- [x] T009 [US1] `web/src/hooks/useWebSocket.ts`: current-connection readings only, `reconnecting`, 5 s quiet close, 1 s back-off, reconnect on visible, pageshow and online
- [x] T010 [US1] `web/src/components/LiveWaiting.tsx`: the one waiting state (Connecting, Reconnecting); strings in `en.json` and `en.context.json`; the Status card's freshness row and `useQuiet` removed (no longer reachable)
- [x] T011 [US1] Dashboard, System and Diagnostics (and the System diagnostics card) use it; the page-load document only while connecting; remove the greyed stale styling
- [x] T012 [US1] `roofView`: "Connecting" pill with no status; `move.clears.live` reworded
- [x] T013 [US1] Component tests `web/src/components/__tests__/DashboardLive.test.tsx`, `System.test.tsx`, `Diagnostics.test.tsx`: the waiting state before the first reading and after a drop (the demo's stream can't be held back from a browser test: its socket is answered inside the page)

## Phase 5: User Story 3 - Consistent secondary text (P2)

- [x] T014 [US3] Tokens and one grouped rule in `web/src/index.css`; `.system-subtle` replaced by `.subtext`; a note after a reading row sits with it; duplicate calibration tag in Diagnostics' move list removed
- [x] T015 [US3] DS-15 in `docs/development/coding-standards.md`
- [x] T016 [US3] Playwright `web/tests/consistency.spec.ts`: every secondary class computes the same size; a note after a row has no divider between them

## Phase 6: User Story 4 - Tooltips on top (P2)

- [x] T017 [US4] `InfoTip` in the top layer with clamped placement (`web/src/components/InfoTip.tsx`, `web/src/lib/tipPlace.ts`, `index.css`), Vitest for the placement helper
- [x] T018 [US4] Playwright `web/tests/consistency.spec.ts`: the Status card's tip is in the top layer and inside the window, also near the edge on a phone

## Phase 7: Polish

- [x] T019 Pin the wizard observatory-branch test's clock to night (`web/tests/wizard.spec.ts`)
- [x] T020 Docs: `docs/api/websocket.md`, `docs/user-guide/diagnostics.md`, `docs/user-guide/dashboard.md`, `docs/api/integrations.md` (diagram re-confirmed)
- [x] T021 Size check: gzipped JS + CSS not above T001 (shared strings where the text is the same); record the baseline
- [x] T022 Full checks (quickstart §1 of spec 001), quality gate, docs build

## Dependencies

T002 → T003 → T004, T005. T006 → T007. T008 → T009 → T010 → T011. T014 → T015, T016. T017 → T018.
US1 and US2 are independent of US3 and US4.
