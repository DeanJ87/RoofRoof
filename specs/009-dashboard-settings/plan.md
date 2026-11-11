# Implementation Plan: Dashboard and Settings

**Branch**: `spec/009-dashboard-settings` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

Web UI only; no firmware, `lib/` or API change.

- `web/src/lib/roofView.ts` (new, pure): from the status, the live-data state and the last command
  result, decide each button's availability and reason, the card's notes and pills, whether the
  Stop bar shows, and the Hall scale's geometry. Every rule in FR-003 to FR-008, FR-012 to FR-014
  is a function here with Vitest tests.
- `web/src/hooks/useRoofCommand.ts` (new): sends a command through `roofCommand()` (the one helper
  with the `X-RoofRoof` header), reads the answer into a command result, and expires it.
- `web/src/dashboard/RoofCard.tsx` (new): the Roof card, composed of `Card`, `Pill`,
  `ReadingRow`, `Button`, `Note`, `InfoTip` and the position bar.
- `web/src/dashboard/HallScale.tsx` (new): the Hall reading on its scale with the thresholds, used
  by the card and the Roof tab.
- `web/src/components/RoofStopBar.tsx` (new): the Stop bar in `Layout`, on the shared `/ws/status`
  socket.
- `web/src/dashboard/glance.ts`: the `roof-fault` and `roof-close` Status rows.
- `web/src/components/settings/RoofTab.tsx` (new): the Roof tab's cards; `tabs.ts` (the tab, error
  routing, section links), `payload.ts` (carry `roof`, `safety`, `park`), `Settings.tsx`.
- Dashboard wiring: `cards.tsx` (the Roof card, given the config and live-data state), `order.ts`
  (`roof` after `status`), `Dashboard.tsx`.
- `index.css`: layout rules for the position bar, the Hall scale, the button row and the Stop bar,
  using existing tokens; the Stop bar shares the save bar's surface rule.
- `inventory.json`: `status.roof.**` mapped field by field; D-40 and D-41 shown on their Settings
  row.
- i18n (`en.json`, `en.context.json`), docs (dashboard, configuration, calibration, roof motion),
  screenshots, Playwright (dashboard, settings tabs, a11y, layout overlap).

Decisions are in [research.md](research.md); the view model in [data-model.md](data-model.md); the
UI contract in [contracts/ui.md](contracts/ui.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | TypeScript strict, Preact 10, Vite |
| **Primary Dependencies** | the existing design-system components, zod schemas (`roofSchema.ts`, `configSchema.ts`), `useWebSocket` (shared socket), `roofCommand()` |
| **Storage** | none new; card order stays per browser |
| **Testing** | Vitest (+ MSW) for the view model, hook, card, Stop bar, glance items and Roof tab; Playwright against the demo for the dashboard inventory, tabs, a11y, overlap and screenshots |
| **Target Platform** | the device's web UI (LittleFS) and the demo |
| **Project Type** | web UI + docs |
| **Performance Goals** | the card follows each `/ws/status` message (about 1 s, faster while moving) |
| **Constraints** | SIZE-01 (≤ 10 KB gzipped growth), DS-01 to DS-28, A11Y rules, functions ≤ 60 lines and complexity ≤ 15 |
| **Scale/Scope** | ~700 lines of UI, ~600 lines of tests, docs |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | The UI never decides a move: it predicts refusals only to disable, and the device's checks are unchanged. No Open or Close without fresh live data. Clear fault is a person's button. The Roof tab says saves are refused while moving. | `roofView.test.ts`: `no live data`, `fault`, `stale`; `RoofCard.test.tsx` |
| II. Parked mount | Close is disabled with the park check's text whenever the status says the park doesn't hold; the device still checks. | `roofView.test.ts`: `close needs the park` |
| III. Unsafe ends closed | Open disabled with the verdict's text unless safe; weather and park are read-only; the card warns that Stop calls off the automatic close. Device refusal texts are shown as sent (translated when known). | `roofView.test.ts`: `open needs safe weather`, `automatic close note`; `RoofCard.test.tsx`: `refusal text` |
| IV. Alpaca | Untouched. | — |
| V. Pure logic | Every decision is in `roofView.ts` with Vitest tests; components only render it. | `roofView.test.ts` |
| VI. Budgets | Web only; SIZE-01 measured with `tools/ui/size_check.py`. | size check |
| VII. UI | Shared components and tokens only; Stop never disabled (no `busy`, no `disabled`); the Stop bar on every page while moving; each disabled button has its reason; strings in i18n; axe and phone width. | `roofView.test.ts`: `stop is always enabled` (every combination); `RoofStopBar.test.tsx`; Playwright a11y and overlap |
| VIII. Security | Every command through `roofCommand()`: `POST`, no body, `X-RoofRoof: 1`, the browser's login. No new route. | `roofCommand.test.ts`, `useRoofCommand.test.ts` |
| IX. Docs | Dashboard, configuration, calibration and roof-motion pages; screenshots list. | `mkdocs build --strict`, DS-PATH |
| X. Quality | check.py, lint, Prettier. | check.py |
| XI. Heritage | The Roof card and tab follow SQMeter's card and settings patterns; nothing copied changes. | — |

**Gate**: pass. One design note, not an exception: DS-01 forbids bars in the header and between the
header and the cards; the Stop bar is pinned to the bottom of the page instead, because
constitution VII requires Stop on every page while the roof moves.

## Project Structure

```text
specs/009-dashboard-settings/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/ui.md
└── checklists/requirements.md

web/src/lib/roofView.ts                      (new) view model
web/src/hooks/useRoofCommand.ts              (new) commands and results
web/src/dashboard/RoofCard.tsx               (new) the Roof card
web/src/dashboard/HallScale.tsx              (new) the Hall scale
web/src/components/RoofStopBar.tsx           (new) Stop on every page
web/src/components/settings/RoofTab.tsx      (new) the Roof tab
web/src/components/Layout.tsx                the Stop bar
web/src/components/Dashboard.tsx, dashboard/cards.tsx, dashboard/order.ts
web/src/dashboard/glance.ts, inventory.json
web/src/components/Settings.tsx, settings/tabs.ts, settings/payload.ts
web/src/index.css, web/src/i18n/en.json, en.context.json
web/tests/dashboard.spec.ts, settings-tabs.spec.ts, a11y.spec.ts, layout-overlap.spec.ts, screenshots.spec.ts
docs/user-guide/dashboard.md, configuration.md, roof-motion.md; docs/hardware/calibration.md
```

**Structure Decision**: the existing web UI layout; the view model sits in `web/src/lib` with the
other pure helpers, the card beside the Status and Device cards in `web/src/dashboard`.

## Complexity Tracking

None.
