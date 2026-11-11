# Tasks: Advanced Diagnostics

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).
Web logic in Vitest; pages in Playwright.

**Organisation**: the pure logic first (settings, recorder, store, documents), then firmware, demo,
web and docs.

## Phase 1: Setup

- [X] T001 Add "017" to `specs/ROADMAP.md`; create `lib/DiagLogic` (`library.json`, headers per data-model.md)

## Phase 2: Tests first

- [X] T002 [US3] `test/test_config_diag`: defaults all off; round trip; a stored document without `diag` loads all off; partial save keeps `diag`; `diag` in `toJson`; config-ranges fixture entries
- [X] T003 [US1] [US2] [US3] `test/test_diag_recorder`: a move from rest to arrival gives one summary (id, origin, target, result `arrived`, duration, driven time, Hall at start and end); stopped and fault endings (fault kind, measured, limit, stall current from the last 13 driven readings; null without driving or current sense); peak and average current only while driven; NaN current and supply skipped (null, never 0); rest volts from the last full 1 s block before driving, sag; ramp phases in order (`deadTime`, `softStart`, `cruise`, `slowing`, `creep`; `slow` from unknown); a move under way at attach isn't recorded; opening→closing directly gives two moves; the ring keeps 5 and overwrites the oldest; ids increase and `setNextId` continues; `copy` of an unknown id false; an active move is copyable with its profile so far
- [X] T004 [US2] `test/test_diag_recorder`: profile of a 50 s move has 80-120 points covering the move; a 180 s move fits in 120; merge rules (average, max, min, duty; a missing side takes the other); no point straddles a boundary after a merge (point starts are multiples of the interval); the last partial point flushed at the end
- [X] T005 [US3] `test/test_diag_recorder`: `test_step_work_bounded`: no allocation in `step()` (counting `operator new` in the test binary) over a whole long move; the worst single step (a merge) under 50 µs on the host; `sizeof(MoveLog) + sizeof(SummaryList)` ≤ 8192
- [X] T006 [US4] `test/test_diag_store`: kept file round trip (every field, NaN kept); bad magic, version, CRC, count or length ignored; at most 32 records; `nextId` continues after the highest id; `KeptWrite` due only with the setting on, a changed revision and 5 minutes since the last write; `SummaryList` newest first, capped at 32, epochs filled only with the clock set, `clear` bumps the revision
- [X] T007 [US5] `test/test_diag_store`: `MqttSchedule`: nothing due while busy; the live document every 60 s at rest; summaries newer than the last published due once each
- [X] T008 [US1] [US2] [US3] `test/test_diag_documents`: the live document's keys and nulls (no current sense, unreadable supply, position unknown, demo values); phase, direction, hall names; power and sag; the off document is exactly `{"enabled":false}`; the summary JSON (origin and result names, fault object, kept summaries with null uptime); the move JSON (columns, points in physical units, nulls); the CSV (header, one row per point, empty fields for null); the live pool size fits `LIVE_DOC_CAPACITY`

## Phase 3: Pure logic

- [X] T009 `lib/ConfigModel` `DiagConfig` until T002 passes; settings-deps catalogue entries for `diag.keepSummaries` and `diag.mqtt` (needs `diag.enabled`; `diag.mqtt` also needs MQTT) with fixtures
- [X] T010 `lib/DiagLogic` `MoveLog` (recorder, profile, phase, rest, stall) until T003-T005 pass
- [X] T011 `lib/DiagLogic` `SummaryList`, `KeptSummaries`, `KeptWrite`, `MqttSchedule` until T006-T007 pass
- [X] T012 `lib/DiagLogic` documents (`writeLive`, `writeSummary`, move JSON and CSV parts) until T008 passes
- [X] T013 `lib/RoofLogic` `Controller::driveBudgetUsedMs(End)` (read-only)

## Phase 4: Firmware (US1-US5)

- [X] T014 `RoofControl`: `attachDiag`/`detachDiag` under the lock; `diagLog->step(sample)` at the end of `stepLocked` (after the output, fault sync and snapshot); the last accepted move's origin; the step timer in `run()` only while attached; `diagLive`, `diagMove`, `diagIds`, `diagLatest`, `diagFinished`, `diagClear` (refused while moving or a close waits) in `src/RoofControlDiag.cpp`
- [X] T015 `Diagnostics` (main loop): allocate and attach on enable, detach and free on disable (and `log: "unavailable"` when allocation fails); the loop pass timer; collect finished moves into the `SummaryList` with the epochs; load the kept file at start and on enable; write it through `PlatformGuard::FlashWrite` when `KeptWrite` is due; remove it after a clear; `main.cpp` wiring (begin after the filesystem, `updateConfig` in the save callback, pass timer in `loop()`)
- [X] T016 `WebServer` (`src/WebServerDiag.cpp`): `GET /api/diag`, `GET /api/diag/moves`, `GET /api/diag/moves/{id}` (JSON, `?format=csv`) as chunked replies from copies, `POST /api/diag/moves/clear` (`requireAuth`, command header, 409 while moving); registered before `/api/diag`'s prefix; `/ws/diag` (closed when off; 250 ms moving / 2 s rest; client cap and stall close); MQTT `<base>/diag` and `<base>/diag/move` through `MqttSchedule`, nothing while `roofBusyForMqtt()`
- [X] T017 `pio run -e esp32dev` and `pio run -e esp32dev_bench` with zero warnings; flash measured (`flash_budget.py`, baseline reason if over 15 KB)

## Phase 5: Demo core (US6)

- [X] T018 `tools/demo-core`: the emulated roof owns a `MoveLog` while `diag.enabled` and steps it after the output; the emulated device keeps the `SummaryList` and serves the documents (`diagDocument`, `diagMoves`, `diagMove(id, csv)`, `diagClear`); bridge exports; `bash tools/demo-core/build.sh`

## Phase 6: Web (US1-US6)

- [X] T019 [P] Types (`DiagConfig`, `DiagLive`, `DiagSummary`, `DiagMove`), zod schema for `diag`, mocks, demo handlers for the four routes and a `/ws/diag` link (250 ms moving, 2 s rest), `lib/diagApi.ts`
- [X] T020 Settings → Device: Diagnostics card (three toggles with "?", the dependent two with their reason while off); config-ranges and settings-deps labels
- [X] T021 System page: Diagnostics card (shown only while on: phase, supply, current, step time, link to `/diagnostics`)
- [X] T022 `/diagnostics` page: off state with a link to the setting; live tiles (roof, control task, loop, memory, network); small-multiple chart of current, supply and duty for the move in progress (stream) or a chosen move (profile), with a text summary; move list with result pills; CSV and JSON downloads; Clear log with its reason while unavailable; i18n with context notes
- [X] T023 Vitest: chart scales and paths, the live buffer (bounded), the page's off state, the list and downloads, the settings card's dependent toggles; Playwright `tests/diagnostics.spec.ts` (demo: enable, move, chart, list, CSV link; axe at phone width); a11y inventory and screenshots if listed
- [X] T024 `web` typecheck, lint, tests, build; UI size (`size_check.py`, baseline reason if over 10 KB)

## Phase 7: Interfaces and docs

- [X] T025 `tools/api/routes.json`, `operations.json`, examples; `python3 tools/api/openapi.py`; contract samples and `tools/contracts/generate_schemas.py` (diag, diag-moves, diag-move, config)
- [X] T026 Docs: `docs/user-guide/diagnostics.md` (new, in `mkdocs.yml`), `docs/api/rest.md`, `docs/api/websocket.md`, `docs/user-guide/mqtt.md`, `docs/user-guide/configuration.md`, `docs/reference/settings-dependencies.md`, `docs/live-demo.md`; CHANGELOG
- [ ] T027 Quickstart §1 of spec 001 in full (firmware, native, web, Playwright, quality gate, flash, docs build); `source_hash.py --check`

## Dependencies

T001 → T002-T008 (tests) → T009-T013 → T014-T017 → T018 → T019-T024 → T025-T027. T019 can start
with T014. Docs (T026) can start once the contract is fixed.

## Parallel examples

- T003-T008 are separate test files or separate groups in one file: written together.
- T019-T021 (types, mocks, settings card, System card) in parallel with T018.

## Implementation strategy

MVP: US1-US3 (live document, log, inert when off) on the firmware and the demo; then US4 (kept
summaries), US5 (MQTT), and the page polish.

## Phase 8: Convergence

- [X] T028 Bring data-model.md and research.md in line with the code: kept records are 56 bytes (1,808-byte file at most) with the header's version, count and next id inside the CRC; the live document's pool is 2,560 bytes and summaries use a heap pool; the heap while on is about 8.5 KB (log, list, one copy buffer) per plan: memory decision (partial)
- [X] T029 Record in the plan's structure the files as built (`src/ResetReason.cpp`, `tools/demo-core/emulated_device_diag.cpp`, `web/src/lib/diagChart.ts`, `web/src/components/diagnostics/*`) per plan: project structure (partial)
