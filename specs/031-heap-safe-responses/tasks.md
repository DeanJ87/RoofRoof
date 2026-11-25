# Tasks: Heap-Safe Responses

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/busy-reply.md](contracts/busy-reply.md)

## Phase 1: Setup

- [ ] T001 Create `lib/HeapSafe` (library.json, include/, src/) and add `-Ilib/HeapSafe/include` to `[env:native]` in platformio.ini

## Phase 2: Foundational (pure logic, tests first)

- [ ] T002 [P] Write `test/test_heap_safe/test_main.cpp`: ChunkText (pieces, read by offset, cap, failure sticky, move), JsonStream (members, elements, nesting, empty sections, overflow flag), BufferedWriter (batching, flush, short write), guard (essential paths, limits at the boundary, frameFits)
- [ ] T003 Implement `lib/HeapSafe/include/ChunkText.h` and `src/ChunkText.cpp`
- [ ] T004 Implement `lib/HeapSafe/include/JsonStream.h` and `src/JsonStream.cpp`
- [ ] T005 Implement `lib/HeapSafe/include/BufferedWriter.h`
- [ ] T006 Implement `lib/HeapSafe/include/HeapGuard.h` and `src/HeapGuard.cpp`
- [ ] T007 Firmware glue `src/WebReply.h`, `src/WebReply.cpp`: ChunkTextResponse, sendText, sendJson, sendBusy, socketText/socketTextAll, guard middleware, counters

## Phase 3: User Story 1 - survive many web clients (P1)

- [ ] T008 [US1] `Core::STATUS_SECTION_CAPACITY` (3 KB) in lib/DeviceCore/include/DeviceCore.h; `test/test_device_core/test_status_document.cpp` measures each status section group against it
- [ ] T009 [US1] `WebServer::writeStatus(ChunkText &)` in sections (src/WebServerStatus.cpp), used by `handleGetStatus` (src/WebServerApi.cpp) and the status WebSocket (src/WebServerLive.cpp)
- [ ] T010 [US1] `writeDiagLive(ChunkText &)`, `/api/diag` and `/ws/diag` through the helper (src/WebServerDiag.cpp)
- [ ] T011 [US1] `/api/alerts/recent` streamed per record; other alert replies through `sendJson` (src/WebServerAlerts.cpp)
- [ ] T012 [US1] Convert src/WebServerApi.cpp (settings/effective, config save answer, Wi-Fi connect and scan, MQTT test), src/WebServerPins.cpp, src/WebServerSite.cpp, src/WebServerTeach.cpp, src/WebServerRoof.cpp replies to `sendJson`; `createErrorJson` from a stack buffer (src/WebServer.cpp)
- [ ] T013 [US1] Crash text into ChunkText: lib/CrashLogic CrashDocuments sinks, src/CrashLog.cpp, src/WebServerCrash.cpp; adapt test/test_crash_*
- [ ] T014 [US1] `MQTTClient::publishJson` (src/MQTTClient.cpp) with BufferedWriter; roof state/result (src/WebServerRoof.cpp), diagnostics (src/WebServerDiag.cpp) and discovery (src/WebServerStatus.cpp, lib/HaDiscovery forEach hands the document; adapt test/test_ha_discovery)
- [ ] T015 [US1] `connections.shortOfMemory` in status (src/WebServerStatus.cpp), status test model, contract schema

## Phase 4: User Story 2 - Alpaca and Stop never starved (P1)

- [ ] T016 [US2] Register the guard middleware in src/WebServer.cpp before the routes
- [ ] T017 [US2] lib/AlpacaLogic RouterInternal.h: reserve from `measureJson`

## Phase 5: User Story 3 - the pattern doesn't come back (P2)

- [ ] T018 [US3] `tools/firmware/test_heap_audit.py` with its exceptions; add to the CI step in .github/workflows/build.yml

## Phase 6: Polish

- [ ] T019 Docs: docs/api/rest.md (503 busy), docs/api/websocket.md (skipped frames), status fields, docs/development/coding-standards.md or a developer note on replies, CHANGELOG.md, docs/development/heritage.md (upstream candidate), specs/ROADMAP.md entry
- [ ] T020 OpenAPI and contract schemas regenerated (`tools/api/openapi.py`, `tools/contracts/generate_schemas.py`); demo core rebuilt (`tools/demo-core/build.sh`)
- [ ] T021 Full quickstart §1 checks: native tests, firmware build, flash budget, quality gate, web checks, docs build
- [ ] T022 Bench: flash (closed, at rest), 5-minute load test with 6 clients, crash records read, counters read
