# Implementation Plan: Honest Live State and Consistent Secondary Text

**Branch**: `spec/028-live-state-and-consistency` | **Date**: 2026-11-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/028-live-state-and-consistency/spec.md`

## Summary

One live schedule for both WebSocket streams, defined in `lib/ConnectionBudget` and mirrored by the
demo; a live-stream hook that only hands out readings from the current connection, reports
connecting or reconnecting, closes a quiet socket and reconnects at once when a page wakes; one
waiting state on the dashboard, System and diagnostics pages; secondary-text tokens applied to every
secondary text class; "?" tips in the top layer. The device bundle must not grow.

## Technical Context

**Language/Version**: C++17 (firmware, native tests), TypeScript strict (Preact web UI)

**Primary Dependencies**: ESPAsyncWebServer (AsyncWebSocket), Preact, MSW (demo), Playwright

**Storage**: N/A (no setting changes)

**Testing**: Unity (`pio test -e native`), Vitest, Playwright against the demo

**Target Platform**: ESP32 (DevKit, WT32-ETH01); current browsers (popover supported in Chrome 114+,
Safari 17+, Firefox 125+)

**Project Type**: embedded firmware + web UI

**Performance Goals**: 2 messages a second per stream while moving, 1 every 2 s at rest

**Constraints**: LittleFS at its 75% limit (no growth); flash under 95% of the OTA slot; the
connection budget (3 sockets per endpoint, 2 endpoints)

**Scale/Scope**: one firmware file per stream, one hook, three pages, CSS tokens, one component

## Constitution Check

| Principle | How it is kept |
|---|---|
| I Motion Safety | No motion change. The broadcast runs on the main loop, not the control task; a busy network still can't prolong a move. |
| II Parked mount | Not touched. |
| III Unsafe ends closed | Not touched. The web UI still only reports the verdict. |
| IV Alpaca | Not touched. |
| V Testable logic | The schedule is a pure function in `lib/ConnectionBudget` with native tests; the hook's phase and back-off are pure helpers with Vitest; pages with Playwright. |
| VI Budgets | The status stream sends twice a second while moving (about 8 KB/s per client); heap use per message unchanged (bench check of `minFreeHeap` during a move). UI gzipped JS + CSS no larger than main. |
| VII UI | Stop stays reachable: with no reading the Stop bar shows (existing rule). Tokens added per DS-10; DS-15 documents them. Explanations stay in "?". |
| VIII Security | Not touched. |
| IX Docs | WebSocket API, diagnostics guide, dashboard guide, integrations diagram, coding standards (DS-15). |
| X Quality | Quality gate run. |

No constitution amendment.

## Project Structure

### Documentation (this feature)

```text
specs/028-live-state-and-consistency/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/live-streams.md
├── checklists/requirements.md
└── tasks.md
```

### Source Code (repository root)

```text
lib/ConnectionBudget/include/ConnectionBudget.h   # the live schedule
lib/DiagLogic/src/DiagSchedule.cpp                 # diag stream uses it
src/WebServer.cpp, src/WebServerLive.cpp           # status stream uses it
test/test_connection_budget/                       # native tests
web/src/hooks/useWebSocket.ts                      # phase, quiet close, wake reconnect
web/src/lib/liveStream.ts                          # pure helpers + demo schedule
web/src/components/LiveWaiting.tsx                 # the one waiting state
web/src/hooks/useDiagLive.ts                       # the diagnostics stream, no page-load fallback after a drop
web/src/components/{Dashboard,System,Diagnostics}.tsx
web/src/lib/roofView.ts                            # "Connecting" pill
web/src/components/InfoTip.tsx, web/src/lib/tipPlace.ts, web/src/index.css (tokens)
web/src/demo/handlers.ts                           # demo streams follow the schedule
web/tests/{consistency,wizard}.spec.ts, component tests in web/src/components/__tests__
docs/api/websocket.md, docs/user-guide/{diagnostics,dashboard}.md, docs/api/integrations.md,
docs/development/coding-standards.md, docs/development/heritage.md
```

**Structure Decision**: existing layout; no new library.

## Complexity Tracking

None.
