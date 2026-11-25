# Tasks: The Roof Card Says Each Fact Once

- [x] T001 Before screenshots of the Roof card on the demo with SQMeter offline (roof open, roof closed)
- [x] T002 `Blocker.short`, with a test per blocker the card can show (`web/src/lib/moveBlockers.ts`)
- [x] T003 `roofView`: blocked as a boolean, the fault reason as the state label (`web/src/lib/roofView.ts`)
- [x] T004 Roof card buttons: one line per button from the blockers, none where the pill or the fault says it, a tooltip instead (`RoofButtons.tsx`)
- [x] T005 Short source health with the device's reason behind "?", joined on Diagnostics (`sourceHealth.ts`, `RoofCard.tsx`, `LiveCards.tsx`)
- [x] T006 The weather hold as one short line; the stand-down as one line with Release hold (`MoveBlockers` `brief`)
- [x] T007 The kept location apart from why: "?" on the dashboard, two notes in Settings (`locationView.ts`, `LocationFields.tsx`)
- [x] T008 Strings: run-on ones removed, short ones added with context; dashboard inventory notes
- [x] T009 Vitest and Playwright updated, a Playwright test for a silent SQMeter
- [x] T010 Docs (dashboard, close-on-unsafe), changelog, roadmap; size check, quality gate, docs build; after screenshots
