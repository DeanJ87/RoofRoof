# Data Model: Heap-Safe Responses

## HeapSafe::ChunkText

| Field | Type | Notes |
|---|---|---|
| `pieces` | `char *[MAX_PIECES]` | `MAX_PIECES` = 24, each `PIECE_BYTES` = 1024 from `malloc`; null until used |
| `length` | `size_t` | bytes written |
| `failed` | `bool` | set when a piece couldn't be allocated or the cap was reached; later writes do nothing |

Operations: `write(uint8_t)`, `write(const uint8_t *, size_t)` (ArduinoJson writer), `append(const
char *)`, `append(const char *, size_t)`, `+=`, `ok()`, `size()`, `read(offset, out, max)`,
`clear()`, `str()` (a `std::string` copy, for tests and small logs). Move-only; frees its pieces.

## HeapSafe::JsonStream

Writes into a `ChunkText`: `beginObject()`, `endObject()`, `beginArray(key)`, `endArray()`,
`members(doc)` (the document's object members, without braces), `element(doc)` (one array
element), `literal(key, text)`. Keeps the comma state per level (8 levels). `overflowed()` when any
document written had overflowed.

## HeapSafe::BufferedWriter<Out, N>

Stack buffer of `N` bytes in front of `Out::write(const uint8_t *, size_t)`; `flush()` at the end;
`failed()` when a write came back short.

## HeapSafe guard

| Constant | Value |
|---|---|
| `MIN_BLOCK_BYTES` | 6,144 |
| `MIN_FREE_BYTES` | 24,576 |
| `FRAME_MARGIN_BYTES` | 4,096 |

`essential(path, isPost)`: `/api/v1/`, `/management/`, `/setup`, `/api/update` (both uploads), and
`POST /api/roof/` + anything. `admit(...)`: essential, or both limits met. `frameFits(length,
largest)`: `largest >= length + FRAME_MARGIN_BYTES`.

## Status fields (new)

`connections.shortOfMemory.refused` (uint32): requests refused by the guard or answered 503 because
an allocation failed, since start. `connections.shortOfMemory.skippedFrames` (uint32): live frames
(status and diagnostics) not sent for memory.
