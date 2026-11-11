# Implementation Plan: Advanced Diagnostics

**Branch**: `spec/017-diagnostics` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/DiagLogic` (new, pure): the per-step move recorder (`Diag::MoveLog`: the move's summary,
  the downsampled profile with pairwise merging, the ramp phase, the supply at rest and the stall
  window), the summary list, the kept-summaries file format with its write decision, the MQTT
  schedule, and the documents (live, list, move JSON and CSV). Native tests first.
- `lib/ConfigModel`: `DiagConfig` (`diag.enabled`, `diag.keepSummaries`, `diag.mqtt`), all off.
- `lib/RoofLogic`: one read-only accessor, `Controller::driveBudgetUsedMs(End)`.
- Firmware: `RoofControl` gets an attachable log (stepped at the end of `stepLocked`, after the
  output is applied), the step-time measurement while one is attached, and bounded copies for the
  readers (`src/RoofControlDiag.cpp`). `Diagnostics` (new, main loop) allocates and frees the log,
  measures the loop pass, collects finished moves into the summary list and writes the kept file
  through `PlatformGuard::FlashWrite`. `WebServer` serves `/api/diag*` and `/ws/diag` and
  publishes `<base>/diag*` at rest (`src/WebServerDiag.cpp`).
- Demo core: the simulated roof steps the same `Diag::MoveLog`; the emulated device serves the
  same documents; the MSW handlers and a `/ws/diag` link.
- Web: Settings → Device's Diagnostics card, the System page's Diagnostics card, the
  `/diagnostics` page (tiles, small-multiple charts, move list, downloads, Clear), i18n, mocks.
- Route registry, OpenAPI, contract schemas; docs (a Diagnostics guide, REST, WebSocket, MQTT,
  configuration), CHANGELOG, roadmap.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the
interfaces in [contracts/diagnostics.md](contracts/diagnostics.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 002's `Roof::Controller`; spec 003's `RoofControl` task and lock; spec 008's MQTT busy rules and `PassBudget`; spec 010's `PlatformGuard::FlashWrite`; spec 011's demo core and bench roof |
| **Storage** | NVS: one small section in the main config document. LittleFS: `/diag-moves.bin` (at most 2 KB), only with `diag.keepSummaries` |
| **Testing** | `pio test -e native` (new `test_diag_recorder`, `test_diag_documents`, `test_diag_store`, `test_config_diag`); Vitest (page logic, chart scales, settings card); Playwright (`tests/diagnostics.spec.ts`, axe at phone width) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | stream every 250 ms while moving; recorder O(1) per step plus one 120-point merge when full |
| **Constraints** | control step unchanged with the mode off (one null check); recording after the output; no allocation, lock or I/O in the step; heap: none with the mode off, at most 8 KB on; flash within SIZE-04 (growth over 15 KB recorded); UI growth under SIZE-01's 10 KB or recorded |
| **Scale/Scope** | 5 moves × 120 points, 32 summaries, 3 stream clients |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | The recorder runs inside `stepLocked` after `applyOutput()` and `syncFault()`, under the lock the step already holds, allocation-free and bounded; with no log attached it is one pointer check and the step isn't timed. Nothing it computes is read by the controller, the gates or the watchdog. Kept summaries are written only through `PlatformGuard::FlashWrite` (never while moving, a close waiting or the sequence busy). The log is freed only by the main loop after detaching it under the lock. | `test_diag_recorder` (bounded work, no allocation, never started mid-move); `test_diag_store` (write decision); review of the step order |
| II. Parked mount | Untouched. | - |
| III. Unsafe ends closed | Untouched; diagnostics copy readings and change nothing (FR-024). Clearing the log is refused while the roof moves or a close waits. | `test_diag_documents`; review |
| IV. Alpaca | Untouched. | ConformU CI |
| V. Pure logic | Recorder, merge, phase, sag, stall, summary list, file format, write and MQTT schedules and documents in `lib/DiagLogic` with native tests; page logic in Vitest. | native, Vitest |
| VI. Budgets | Handlers copy one move (about 1 KB) under the roof's lock or the summary list (about 2 KB) under its mutex, then write chunked replies (no large JSON pool). The stream is one ~1.2 KB document per 250 ms at most. MQTT diagnostics publish nothing while busy, so spec 008's 500 ms pass budget is untouched. Heap: 0 off, ~7 KB on (`static_assert` ≤ 8 KB). Flash and UI measured. | build, `flash_budget.py`, `size_check.py`, `test_diag_documents` (pool sizes) |
| VII. UI | Existing cards, tiles, rows and tokens; hidden unless enabled; Stop bar from the layout; i18n; a11y; phone width. | Vitest, Playwright + axe, `copy_check.py`, `label_check.py` |
| VIII. Security | Reads are open like `/api/status` (readings only, no address or secret). Clearing calls `requireAuth` and needs `X-RoofRoof: 1`. | `routes.py`, Vitest of the client, review |
| IX. Docs | Diagnostics guide, REST, WebSocket, MQTT, configuration; OpenAPI regenerated. | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py, function limits. | check.py |
| XI. Heritage | No SQMeter module copied or changed. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/017-advanced-diagnostics/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
lib/DiagLogic/ (new) include/{DiagTypes,MoveLog,SummaryList,KeptSummaries,DiagSchedule,DiagDocuments}.h,
  src/{MoveLog,MovePhase,SummaryList,KeptSummaries,DiagSchedule,DiagLive,DiagMoves}.cpp, library.json
lib/ConfigModel/ Config.h, ConfigInternal.h, ConfigJsonIn.cpp, ConfigJsonOut.cpp, ConfigValidate.cpp
lib/RoofLogic/ include/RoofController.h (driveBudgetUsedMs)
lib/SettingsDeps/ catalogue.json, src/SettingsDeps.cpp (the two dependent settings)
include/RoofControl.h, src/RoofControlDiag.cpp (new), src/RoofControl.cpp, src/RoofControlTask.cpp
include/Diagnostics.h, src/Diagnostics.cpp, include/ResetReason.h, src/ResetReason.cpp (new), src/main.cpp
include/WebServer.h, src/WebServerDiag.cpp (new), src/WebServer.cpp, src/WebServerLive.cpp
tools/demo-core/ emulated_roof.*, emulated_device*.{h,cpp} (new: emulated_device_diag.cpp), bridge.cpp, build.sh
test/test_diag_recorder, test/test_diag_documents, test/test_diag_store, test/test_config_diag (new);
  test/test_config_migration, test/fixtures/config-ranges.json, settings-deps fixtures
web/src: components/Diagnostics.tsx (new), components/diagnostics/{LiveCards,DiagChart}.tsx (new), lib/diagChart.ts (new),
  lib/diagApi.ts (new), types/diag.ts (new), components/settings/DeviceTab.tsx, System.tsx, App.tsx,
  types/config.ts, validation/configSchema.ts, demo (device, handlers), mocks, i18n
web/tests/diagnostics.spec.ts (new), a11y inventory
tools/api/{routes,operations}.json, tools/api/examples, tools/contracts/generate_schemas.py,
  specs/001-platform-bootstrap/contracts/schemas/{config,diag,diag-moves,diag-move}.schema.json,
  docs/api/openapi.json
docs/user-guide/diagnostics.md (new), mkdocs.yml, docs/api/{rest,websocket}.md,
  docs/user-guide/{mqtt,configuration,dashboard}.md, docs/reference/settings-dependencies.md, docs/live-demo.md, CHANGELOG.md, specs/ROADMAP.md
```

**Structure Decision**: one new pure library for everything diagnostics decides, one new main-loop
module that owns the log's memory and the kept file, and the presentation in one new WebServer
file. `RoofControl` gains only the attach point, the copies and the step timer, kept in their own
file so the control task's code stays as it is.

## Phases

1. **Pure logic, tests first**: recorder (summary, profile, merge, phase, rest and sag, stall),
   summary list, kept file, write and MQTT schedules, documents; the settings.
2. **Firmware glue**: attach point, step timer, copies; `Diagnostics`; routes, stream and MQTT;
   zero warnings; flash.
3. **Demo core**: the recorder in the simulated roof; documents; WASM rebuilt.
4. **Web**: settings card, System card, Diagnostics page with charts; Vitest and Playwright;
   contracts, OpenAPI.
5. **Docs**.
6. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
