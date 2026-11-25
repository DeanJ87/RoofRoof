# Implementation Plan: The Roof Card Says Each Fact Once

**Branch**: `spec/034-roof-card-one-fact` | **Spec**: [spec.md](spec.md)

## Summary

Give `Blocker` a `short` label and let the Roof card's buttons show one line from it, the first
blocker per button. `roofView`'s blocked check becomes a boolean (its text came from a second copy
of the reasons). The source health lines split into a short `text` and the device's `detail`
(behind "?" on the card, joined on Diagnostics); the location line splits the kept location from
the problem (`why`). `MoveBlockers` gains `brief` for the stand-down line. Strings that ran two facts
together are removed, so the UI shrinks.

## Constitution check

- I-III (safety): no command, interlock or firmware change; the page still only explains.
- VI (size): the device JS shrinks by about 70 bytes gzipped; no CSS change; LittleFS stays at its
  75% line.
- VII (UI): a disabled button's reason stays on the card (a line, or the state pill and the fault
  block with a tooltip); explanations in "?"; DS-04, DS-08, DS-22, DS-24.
- IX (docs): `docs/user-guide/dashboard.md`, `docs/user-guide/close-on-unsafe.md`, the changelog.

## Files

- `web/src/lib/moveBlockers.ts` (`short`), `web/src/lib/roofView.ts` (blocked as a boolean),
  `web/src/lib/sourceHealth.ts` (`detail`, `healthText`), `web/src/lib/locationView.ts` (`why`).
- `web/src/dashboard/RoofButtons.tsx` (one line per button), `web/src/dashboard/RoofCard.tsx`
  (health rows, daylight row, stand-down), `web/src/components/MoveBlockers.tsx` (`brief`),
  `web/src/components/diagnostics/LiveCards.tsx`, `web/src/components/settings/LocationFields.tsx`.
- `web/src/i18n/en.json` and `en.context.json`; `web/src/dashboard/inventory.json`.
- Tests: `moveBlockers`, `roofView`, `movePlan`, `locationView`, `RoofCard`, `RoofMove` (Vitest);
  `dashboard.spec.ts`, `journey.spec.ts` (Playwright).
