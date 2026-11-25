# Research: Heap-Safe Responses

## R1. What crashed

Spec 029's records from the bench board under `tools/load_config_get.py --clients 6 --minutes 5
--also /api/status /api/diag "/api/v1/dome/0/shutterstatus?..."` (decoded with
`tools/decode_crash.py` and the build's `.elf`): about 11 restarts in 5 minutes, every one on
`async_tcp`, `abort()` from `operator new` inside `std::string::push_back` while `serializeJson(doc,
std::string&)` grew the text, called from `WebServer::createStatusJson()` for `handleGetStatus`.
Heap at the crash: 77-105 KB free, largest block 8-10 KB. The log also showed "/api/status document
overflowed 0 bytes": the 7 KB pool itself had failed to allocate (capacity 0), so the reply was
`null`.

The firmware is built without C++ exceptions (`CONFIG_COMPILER_CXX_EXCEPTIONS=n`), so `operator
new` that fails calls `abort()`. `malloc` returns null instead. ArduinoJson 6's
`DynamicJsonDocument` allocates its pool with `malloc` (capacity 0 on failure); Arduino `String`
uses `realloc` and marks itself invalid on failure; `std::string`, `std::vector`,
`std::make_shared` and `std::function` use `operator new` and abort.

## R2. What a status reply cost before

| Step | Block | Notes |
|---|---|---|
| JSON pool | 7,168 B | `Core::STATUS_DOC_CAPACITY` |
| `std::string` growth | 4,096 + 8,192 B at the last doubling | text 4,257 B on the bench, capacity doubles from 16 |
| `request->send(code, type, json.c_str())` | 4,258 B | `AsyncBasicResponse` copies the text into a `String` |
| send buffer | 2,920 B | the library's own, `new (std::nothrow)` |

Largest single block 8 KB, about 26 KB at once per request; six at once fragment the heap.

## R3. The pattern chosen

**Decision**: a piece buffer (`HeapSafe::ChunkText`): up to 24 pieces of 1 KB from `malloc`, a
fixed array of piece pointers (no container growth), a sticky failure flag, read back by offset.
ArduinoJson 6 serializes into any class with `write(uint8_t)` and `write(const uint8_t *, size_t)`,
so `serializeJson(doc, chunks)` needs no adapter. A response class derived from
`AsyncAbstractResponse` owns the buffer and fills the library's send buffer from it
(`_fillBuffer`); it is allocated with `new (std::nothrow)`. On any failure the reply is the shared
`503`.

**Alternatives considered**:

- `measureJson` then one exact `malloc` (spec 029's `/api/config` pattern): no doubling and no
  abort, but one block the size of the text (4.3 KB for status); kept for `/api/config` (already
  merged and measured), not chosen for the rest.
- `AsyncResponseStream`: backed by a `cbuf` that grows with `new` and resizes: the same contiguous
  growth.
- A chunked response that re-serializes the document for each window: the status changes between
  calls (uptime, heap), so the bytes would not join up.
- ArduinoJson 7 (pool in pages): a major-version change across every document and test; out of
  proportion.

## R4. Sections for large documents

The status pool (7 KB) is itself a large block. `/api/status` is written as one object whose
members come from several groups (firmware and runtime; alerts and Alpaca; memory, partitions,
time, Wi-Fi, Ethernet and pins; MQTT and connections; roof; site). Each group is written into the
same 3 KB pool (`Core::STATUS_SECTION_CAPACITY`), serialized without its outer braces and joined with
commas (`HeapSafe::JsonStream::members`). The pool is allocated once per reply and cleared between
groups. `test_status_document` measures each group on the device's slot size and requires 20%
headroom in 3 KB.

`/api/alerts/recent` (an 8 KB pool before) is written the same way: the head members from a small
pool, then each record as one array element from a 768-byte pool.

## R5. WebSocket frames

`AsyncWebSocket::textAll` copies the message into a `std::make_shared<std::vector<uint8_t>>` (abort on
failure). A frame must be one buffer. Chosen: build the frame in pieces, then call
`makeBuffer(len)` only when `heap_caps_get_largest_free_block` is at least `len + 4 KB`
(`HeapSafe::frameFits`); otherwise skip the frame and count it. The check and the allocation run on
the main loop while `async_tcp` may allocate on the same core at a lower priority, and lwIP on the
other core; the 4 KB margin covers a pbuf burst between the two. The diagnostics frame (2.5 KB pool)
follows the same rule.

## R6. MQTT

PubSubClient 2.8 has `beginPublish(topic, length, retained)`, `write` and `endPublish`: the payload
goes straight to the network client without its 3 KB buffer. `serializeJson` writes punctuation one
byte at a time, so a 128-byte stack buffer (`HeapSafe::BufferedWriter`) batches the writes.
`measureJson` gives the length first. Home Assistant discovery hands each entity's document to the
callback instead of a string (`HaDiscovery::forEach`), and the firmware publishes it the same way.

## R7. The memory guard

ESPAsyncWebServer 3.12 runs server middleware after a request is parsed and before its handler.
One middleware checks the heap: when the largest free block is below 6 KB or the free heap below
24 KB, a non-essential request gets the `503` before any handler allocates. Essential: the Alpaca
API (`/api/v1/`, `/management/`, `/setup`), `POST /api/roof/...` (Stop included), and the upload
routes (`/api/update`, `/api/update/fs`, whose body handlers run before middleware anyway). The rule
is `HeapSafe::admit(path, isPost, largest, free)` in `lib/HeapSafe`.

Why 6 KB: a heap-safe reply needs one 3 KB pool, 1 KB pieces and the library's 2.9 KB send buffer,
each its own block; 6 KB leaves one of each. Why 24 KB free: lwIP, the Wi-Fi driver and the roof's
own work need headroom; the bench idles at about 140 KB free with a 73 KB largest block, and the
crashes happened at 77 KB free with 8 KB blocks, so the guard bites only when fragmentation is
already severe.

## R8. Audit of every reply the firmware builds

Measured on the bench (`curl -w %{size_download}`, board idle, closed) or the document's capacity
where not measurable. "Before" is the largest block the reply needed; "after" is the firmware's own
largest block (the library's 2.9 KB send buffer comes on top in every row).

| Path | Builder | Body | Before | After |
|---|---|---|---|---|
| `GET /api/status` | `createStatusJson` | 4,257 B | 8 KB string + 7 KB pool + 4.3 KB copy | 3 KB pool, 1 KB pieces |
| `/ws/status` frames (2 s, each client's first) | same | 4,257 B | same + 4.3 KB vector | 3 KB pool, pieces, one 4.3 KB frame only if largest ≥ 8.3 KB |
| `GET /api/config` | `settingsJson` (spec 029) | 4,005 B | 9.5 KB pool + exact 4 KB | unchanged (checked, 503) |
| `GET /api/settings/effective` | `Deps::writeReport` | 2,700 B | 4 KB string + pool ~3.5 KB + copy | pool, pieces |
| `GET /api/pins` | `ConfigPins` doc | 2,190 B | 4 KB string + 6 KB pool + copy | 6 KB pool (checked), pieces |
| `POST /api/pins/validate`, `GET/POST /api/pins/check` | pins docs | < 1 KB | string + 3 KB pool | pool (checked), pieces |
| `GET /api/diag` and `/ws/diag` | `diagLiveJson` | 857 B | 1 KB string + 2.5 KB pool + copy | 2.5 KB pool, pieces |
| `GET /api/diag/moves[/id]` | `Diag::*Part` | 70 B - tens of KB | chunked already (small parts) | unchanged |
| `GET /api/diag/crashes` | `Crash::crashesText` | 3,323 B | 4 KB string + copy | pieces |
| `GET /api/diag/log` | `Crash::logText` | 3,483 B | 4 KB string + copy | pieces |
| `GET /api/alerts/recent` | handler | 295 B - ~6 KB | 8 KB string + 8 KB pool + copy | 768 B pool per record, pieces |
| `GET /api/alerts/armed` | literal | 97 B | literal | unchanged |
| `GET /api/roof/inputs` | `Safety::writeInputsReport` | 420 B | 3 KB pool + string | pool (checked), pieces |
| `GET /api/roof/calibration` | `Calib` document | 187 B - ~3 KB | 6 KB pool + string | 6 KB pool (checked), pieces |
| `GET /api/roof/teach` | `Calib` teach doc | 214 B | 1.5 KB pool + string | pool, pieces |
| roof command replies | 512 B docs | < 300 B | string | pieces |
| `GET /api/wifi/scan` | 2 KB stack doc | < 2 KB | string | pieces |
| `POST /api/config` reply, MQTT test, Wi-Fi connect, site routes, teach | small docs | < 500 B | string / `String` | pieces |
| error replies (`createErrorJson`) | 128 B doc | < 200 B | string | fixed 192 B stack buffer |
| `POST /api/backup*` | `Backup::*` | up to ~12 KB | guarded by its own free-memory check | unchanged (audit exception) |
| Alpaca (`/api/v1/...`, `/management/...`) | `lib/AlpacaLogic` router | 94-600 B | doubling string, `String` copy | exact `reserve(measureJson)` then `String` copy (malloc) |
| MQTT `<base>/roof/state` | `RoofIo::Mqtt::writeState` | ~1.5 KB | 3.5 KB pool + 2 KB string | 3.5 KB pool (checked), streamed |
| MQTT `<base>/roof/result` | `writeResult` | < 300 B | 1 KB pool + string | pool, streamed |
| MQTT `<base>/diag`, `<base>/diag/move` | diag docs | < 1 KB | string | pool, streamed |
| MQTT discovery (about 30 entities) | `HaDiscovery::payload` | ~600 B each | 2 KB pool + 1 KB string each | 2 KB pool, streamed |
| Alert messages (outbound) | `AlertDelivery` | < 768 B | string | unchanged (audit exception; not a reply) |
| Stored settings | `ConfigJsonOut` | ~4 KB | string | unchanged (audit exception; NVS write, save guarded) |

## R9. SQMeter

The same pattern exists in SQMeter (read-only here); the list is in the PR and the heritage file
marks `lib/HeapSafe` as an upstream candidate.
