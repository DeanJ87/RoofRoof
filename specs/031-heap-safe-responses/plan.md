# Implementation Plan: Heap-Safe Responses

**Branch**: `spec/031-heap-safe-responses` | **Date**: 2026-11-25 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/HeapSafe` (new, pure, native tests first):
  - `ChunkText`: text in up to 24 pieces of 1 KB from `malloc`, sticky failure, read by offset; an
    ArduinoJson writer and a `+=` text sink.
  - `JsonStream`: writes one JSON object or array into a `ChunkText` from sections, each serialized
    from a reused document without its outer braces, joined with commas.
  - `BufferedWriter<Out, N>`: batches ArduinoJson's byte writes into a stack buffer for a `Print`.
  - `Guard`: `admit(path, isPost, largestBlock, freeHeap)`, `essential(path, isPost)`,
    `frameFits(length, largestBlock)` and the limits (6 KB block, 24 KB free, 4 KB frame margin).
- Firmware glue `src/WebReply.{h,cpp}` (new): the `ChunkTextResponse` (an `AsyncAbstractResponse`
  that owns the pieces), `sendText`, `sendJson`, `sendBusy` (the shared 503), `socketTextAll` and
  `socketText` (frame check then `makeBuffer`), the guard middleware and the two counters.
- Converted: every `serializeJson` into a string in `src/WebServer*.cpp`; `createStatusJson` becomes
  `writeStatus(ChunkText &)` in sections; `diagLiveJson` becomes `writeDiagLive(ChunkText &)`;
  `/api/alerts/recent` streamed per record; crash text into `ChunkText`; `createErrorJson` from a stack
  buffer.
- MQTT: `MQTTClient::publishJson(topic, doc, retained)` streams through `beginPublish` and a
  `BufferedWriter`; roof state and result, diagnostics and discovery use it; `HaDiscovery::forEach`
  hands out the document.
- `lib/AlpacaLogic`: reserve the reply from `measureJson` (no growth).
- `tools/firmware/test_heap_audit.py` (new) in CI beside the ISR audit.
- Status: `connections.shortOfMemory {refused, skippedFrames}`; contract schema, OpenAPI, docs,
  CHANGELOG, heritage.

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); Python 3.10+ |
| **Primary Dependencies** | ArduinoJson 6.21 (custom writer, `measureJson`); ESPAsyncWebServer 3.12.0 (`AsyncAbstractResponse::_fillBuffer`, server middleware, `AsyncWebSocket::makeBuffer`); PubSubClient 2.8 (`beginPublish`/`endPublish`); `heap_caps_get_largest_free_block` |
| **Storage** | none |
| **Testing** | `pio test -e native` (new `test_heap_safe`; `test_status_document` per section; `test_ha_discovery`, `test_crash_documents` adapted); `python3 -m unittest tools/firmware/test_heap_audit.py`; bench load test |
| **Target Platform** | ESP32 DevKit and WT32-ETH01 (same image) |
| **Constraints** | flash growth < 6 KB (90.0% of the slot after spec 030's LTO); no UI/LittleFS change; no handler blocks |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Nothing runs on the roof's task; roof commands are essential (never refused by the guard). | `test_heap_safe` (essential paths) |
| II, III | Untouched. | - |
| IV. Alpaca | Router reply sized once, content unchanged; Alpaca never refused by the guard. | AlpacaLogic native tests, ConformU CI |
| V. Pure logic | Buffer, stream writer, guard rule in `lib/HeapSafe` with native tests. | `test_heap_safe` |
| VI. Budgets | Every reply allocation checked; largest firmware block per reply ≤ 3 KB on the hot paths; flash measured; heap measured under load on the bench. | build, `flash_budget.py`, load test |
| VII. UI | No UI change. | - |
| VIII. Security | Guard runs before auth; it only refuses. No route's auth changes. | `tools/api/routes.py` |
| IX. Docs | REST (503), WebSocket (skipped frames), status fields, developer note. | mkdocs strict, openapi check |
| X. Quality | Function limits; audit in CI. | check.py |
| XI. Heritage | `lib/HeapSafe` marked upstream candidate for SQMeter. | - |

## Project Structure

```text
lib/HeapSafe/{library.json, include/ChunkText.h, include/JsonStream.h, include/BufferedWriter.h,
              include/HeapGuard.h, src/ChunkText.cpp, src/JsonStream.cpp, src/HeapGuard.cpp}
src/WebReply.h, src/WebReply.cpp           (new glue)
src/WebServer*.cpp, src/MQTTClient.cpp, src/CrashLog.cpp   (converted)
lib/DeviceCore/include/DeviceCore.h       (STATUS_SECTION_CAPACITY)
lib/CrashLogic, lib/HaDiscovery, lib/AlpacaLogic, lib/DiagLogic   (sinks)
test/test_heap_safe/                       (new)
tools/firmware/test_heap_audit.py          (new)
```

## Complexity Tracking

None.
