# Feature Specification: Heap-Safe Responses

**Feature Branch**: `spec/031-heap-safe-responses`

**Created**: 2026-11-25

**Status**: Draft

## Context

Spec 029's crash capture showed what restarts the controller under web load. With six web clients
reading the status, the diagnostics, the settings and the Alpaca shutter status at once, the bench
board restarted about eleven times in five minutes. Every record was the same: the network task
(`async_tcp`) called `abort()` because `operator new` failed while a reply was being written into a
growing string. The firmware has no C++ exceptions, so a failed `new` stops the device.

The heap was not empty: 77-105 KB were free, but the largest free block was only 8-10 KB. Each
`/api/status` reply took a 7 KB JSON pool, a text that grows by doubling (16, 32, ... 4096, 8192
bytes, the old and new copies both held while it grows) and then a third copy of the 4.3 KB text
inside the web server's response. Several replies at once cut the heap into pieces too small for
the next doubling. Spec 029 fixed this for `GET /api/config` only; the same pattern is in most
other replies, the live WebSocket frames and the MQTT documents.

This spec makes every reply the device builds safe on a fragmented heap:

- a reply is written into small pieces (1 KB each, every allocation checked) and sent from them;
  when a piece can't be had, the client gets `503` with a "busy, try again" body instead of the
  device stopping;
- the large documents are built a section at a time from one small JSON pool, so no reply needs a
  block larger than about 3 KB of its own;
- a live WebSocket frame is skipped, not sent, when the heap can't hold it;
- MQTT documents stream from their JSON pool straight to the broker, with no text copy;
- when the largest free block is already small, non-essential requests (pages, settings, status
  reads by browsers) are refused with `503` before anything is allocated, so the essential ones
  (Alpaca, roof commands) keep the memory they need;
- a check in CI forbids writing a reply into a growing string again.

Nothing here changes what the roof does or decides. The replies' content stays the same.

## Clarifications

### Session 2026-11-25

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Which requests are essential and never refused by the memory guard? → A: The ASCOM Alpaca API
  (`/api/v1/...`, `/management/...`, `/setup`), every roof command (`POST /api/roof/...`, which
  includes Stop), and the firmware and file-system uploads (they have checks of their own). Every
  other request (the web pages and their files, status, diagnostics, settings, pins, alerts,
  backups, Wi-Fi) is refused with `503` while memory is short. N.I.N.A. must keep reading the
  shutter and Stop must always be heard; a browser retries on its next poll.
- Q: What counts as "memory short"? → A: The largest free block below 6 KB or the free heap below
  24 KB. A reply built in pieces needs at most a 3 KB JSON pool, 1 KB pieces and the web server's
  own 2.9 KB send buffer, each a block of its own; 6 KB leaves room for one of each plus a request.
  These are fixed limits in code, not settings (they are about the firmware, not the roof).
- Q: Does a refused or failed request say so? → A: `503` with `Retry-After: 2` and a JSON body
  `{"error":"The device is busy: try again"}`, the same for every route. A counter of refusals and
  of skipped live frames is in `/api/status` (`connections.shortOfMemory`).
- Q: What about a reply the web server library itself can't allocate (its request object, headers)?
  → A: Out of reach of this spec: those are small (under 1 KB) and the guard keeps the heap from
  getting near them. The spec covers every reply the firmware builds.
- Q: The live status frame must be one block (WebSocket messages are sent from one buffer). → A: It
  is built in pieces like the REST reply, then copied into one buffer only when the largest free
  block is at least the frame plus 4 KB; otherwise the frame is skipped and counted. The next frame
  comes 2 s later.
- Q: Should the Alpaca router (`lib/AlpacaLogic`) change? → A: Only to size its reply exactly once
  (`measureJson` first, one allocation) instead of growing it. Its replies are at most a few hundred
  bytes; its shape and behaviour stay the same (constitution IV; ConformU runs in CI).
- Q: Does the backup reply change? → A: No: it already checks free memory before it builds (spec
  024) and is one request at a time; the CI check lists it as an exception with that reason. The
  settings save is the same (spec 029 R13).
- Q: The alert sender (`lib/DeviceCore` AlertDelivery) builds its outgoing message in a string. →
  A: Not a reply; it runs on its own task with a bounded 768-byte document. Left as it is and listed
  in the CI check's exceptions (spec 030 is changing the alert code).
- Q: Any UI change? → A: No. The pages already retry a failed read on their next poll; the
  LittleFS image is at its limit.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | Stop must always be heard; nothing may delay the roof's task | FR-008: roof commands are essential and never refused by the guard; nothing here runs on the roof's task |
| IV | Alpaca behaves as the standard says | FR-008, FR-012: Alpaca is never refused by the guard; its replies keep their shape (ConformU CI) |
| VI | Handlers don't block; heap measured on a device | FR-001 to FR-007, FR-016: every reply allocation checked, largest block per reply bounded, measured under load on the bench |
| V | Logic is pure and tested | FR-013: the piece buffer, the section writer and the guard's rule live in `lib/HeapSafe` with native tests |
| IX | Docs move with behaviour | FR-017 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The controller survives many web clients (Priority: P1)

The owner has the dashboard open on a laptop and a phone, N.I.N.A. polls the roof over Alpaca, and
Home Assistant reads the status. A third browser opens the Diagnostics page. The controller keeps
running: no restart, no crash record. At worst a page shows its "can't reach the device" note for
one poll and recovers on the next.

**Why this priority**: A crash under ordinary use restarts the roof's controller in the middle of
the night; while it restarts nothing watches the weather.

**Independent Test**: Run `tools/load_config_get.py <board> --clients 6 --minutes 5 --also
/api/status /api/diag "/api/v1/dome/0/shutterstatus?..."` against a bench board at rest: the boot
count doesn't change and `GET /api/diag/crashes` lists no new record.

**Acceptance Scenarios**:

1. **Given** six clients reading the settings, status, diagnostics and the shutter status in a
   loop for five minutes, **When** the run ends, **Then** the device has not restarted.
2. **Given** the heap is fragmented so a reply's piece can't be allocated, **When** a reply is
   built, **Then** the client gets `503` with `Retry-After` and the "busy" body, and the device keeps
   running.
3. **Given** a live status frame doesn't fit the largest free block (plus margin), **When** the
   broadcast runs, **Then** the frame is skipped, the skipped-frame counter rises, and the next
   broadcast tries again.

---

### User Story 2 - N.I.N.A. and Stop are never starved (Priority: P1)

During the same load, N.I.N.A. keeps reading the shutter status and gets an answer each time;
pressing Stop in the web UI is answered.

**Why this priority**: The roof's safety depends on Alpaca and Stop, not on the dashboard.

**Independent Test**: During the load test, every Alpaca shutter-status request returns `200` with
an Alpaca body; with the memory guard tripped (largest block below the limit), a status read
returns `503` while an Alpaca read still returns `200`.

**Acceptance Scenarios**:

1. **Given** memory is short, **When** a browser asks for `/api/status`, **Then** it gets `503`
   before any reply is built.
2. **Given** memory is short, **When** N.I.N.A. asks for the shutter status or a person sends Stop,
   **Then** the request is handled as usual.

---

### User Story 3 - The pattern doesn't come back (Priority: P2)

A later change writes a reply with `serializeJson(doc, someString)`. CI fails and names the file
and line, pointing to the heap-safe helper.

**Why this priority**: The crash came from one pattern repeated in many places; a check keeps it
out.

**Independent Test**: Add `std::string s; serializeJson(doc, s);` to a handler; the audit test
fails naming it.

**Acceptance Scenarios**:

1. **Given** a reply written into a `std::string` or `String` by `serializeJson`, **When** CI runs,
   **Then** the audit fails unless the line is in its list of exceptions with a reason.
2. **Given** a WebSocket text sent from a string, **When** CI runs, **Then** the audit fails
   (frames go through the helper that checks the block first).

### Edge Cases

- A reply larger than the piece buffer's cap (24 KB): the buffer refuses and the client gets `503`;
  no reply today comes near (the largest is 4.3 KB).
- A JSON pool that can't be allocated (capacity 0): the reply is `503`, not "null".
- A section of the status overflows its pool: the members that fit are sent, the overflow is logged
  once per minute at most, and the native test keeps each section within its pool with 20% headroom.
- The client disconnects before the reply is sent: the pieces are freed with the response.
- A WebSocket client connects while memory is short: its first frame is skipped; the next broadcast
  brings it up to date.
- MQTT publish while the broker connection drops halfway: the write fails, the client reconnects as
  today; no buffer is held.
- Many 503s in a row: the counters say so in `/api/status`; the device stays up.

## Requirements *(mandatory)*

### Functional Requirements

Replies

- **FR-001**: Every HTTP reply body the firmware builds from a JSON document or as text MUST be
  written into a piece buffer whose pieces are at most 1 KB and allocated with `malloc` (never
  `operator new`), and sent from those pieces through the web server's callback response; nothing
  grows by doubling and no second copy of the body is made.
- **FR-002**: When a piece or a JSON pool can't be allocated, the reply MUST be `503` with
  `Retry-After: 2` and `{"error":"The device is busy: try again"}`; the device MUST NOT stop.
- **FR-003**: `/api/status` and the live status frame MUST be built a section at a time from one JSON
  pool of at most 3 KB, reused for each section; the reply's content and order stay the same.
- **FR-004**: No single reply MUST need a block larger than about 3 KB of the firmware's own
  (the JSON pool), besides the web server's 2.9 KB send buffer; the numbers are documented per reply.
- **FR-005**: A WebSocket frame (status and diagnostics) MUST be built in pieces and copied into one
  buffer only when the largest free block is at least the frame plus 4 KB; otherwise it is skipped
  and counted. A new client's first frame follows the same rule.
- **FR-006**: MQTT documents (the roof's state and command results, the diagnostics, Home Assistant
  discovery) MUST stream from their JSON pool to the broker through a small fixed buffer, with no
  text copy; a pool that can't be allocated skips that publish.
- **FR-007**: The crash records and the log (`/api/diag/crashes`, `/api/diag/log`) MUST be written
  into the piece buffer too.

Memory guard

- **FR-008**: Before any handler runs, a request MUST be refused with the FR-002 reply when the
  largest free block is below 6 KB or the free heap is below 24 KB, unless it is essential: the
  Alpaca API (`/api/v1/`, `/management/`, `/setup`), a roof command (`POST /api/roof/...`), or a
  firmware or file-system upload.
- **FR-009**: The guard's rule (which requests are essential, the limits) MUST be pure logic with
  native tests.
- **FR-010**: `/api/status` MUST report `connections.shortOfMemory.refused` (requests refused or
  answered 503 for memory since start) and `connections.shortOfMemory.skippedFrames` (live frames
  skipped).

Fragmentation

- **FR-011**: The status reply MUST use one JSON pool per request (reused for each section) and no
  per-request temporary larger than 3 KB; small replies (errors, acknowledgements) are written from
  fixed buffers.

Boundaries

- **FR-012**: The Alpaca router MUST size its reply once from `measureJson` (no growth); its
  replies are otherwise unchanged.
- **FR-013**: The piece buffer, the section writer, the buffered stream writer and the guard rule
  MUST live in `lib/HeapSafe` (no Arduino headers) with native tests written first.
- **FR-014**: A CI-run audit MUST fail on `serializeJson` into a `std::string` or `String` anywhere in
  `src/` and `lib/*/src/` and on WebSocket text sent outside the helper, except for listed lines
  each with its reason.
- **FR-015**: Nothing in this spec MUST change the roof's motion, the gates, the close sequence or
  the safety verdict.
- **FR-016**: The image MUST stay within 95% of its OTA slot with growth under 4 KB; no UI or
  LittleFS change.
- **FR-017**: The REST reference (503 under load), the WebSocket reference (skipped frames), the
  status fields, a developer note on writing replies, the CHANGELOG and the heritage file (an
  upstream candidate for SQMeter) MUST be updated.

### Key Entities

- **Piece buffer (ChunkText)**: a text held in up to 24 pieces of 1 KB; remembers a failed
  allocation; read back by offset.
- **Section writer**: writes one JSON object from several sections, each serialized from the same
  small pool and joined without the outer braces.
- **Memory guard**: the rule deciding, from the request and the heap figures, whether a request may
  run.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Six clients for five minutes (settings, status, diagnostics, Alpaca shutter status)
  cause no restart on the bench board (it restarted about 11 times before).
- **SC-002**: Every Alpaca shutter-status read in that run is answered `200`.
- **SC-003**: No reply needs a firmware-owned block above 3 KB; `/api/status` needs 3 KB instead of
  7 KB + 8 KB + 4.3 KB.
- **SC-004**: The image grows by less than 4 KB and stays below 95% of the slot; the UI is unchanged.
- **SC-005**: The audit test fails on the old pattern and passes on the branch.

## Assumptions decided for the owner

- Essential requests are Alpaca, roof commands and uploads; everything else may get `503` while the
  largest free block is below 6 KB or the free heap below 24 KB. The limits are code constants.
- A `503` says "The device is busy: try again" with `Retry-After: 2`; the pages already retry on
  their next poll, so there is no UI change.
- A live frame that doesn't fit is skipped, not queued; the counters are in `/api/status` only (no
  alert, no UI): a busy dashboard is not a fault.
- The web server library's own small allocations (request objects, headers) can still fail on an
  exhausted heap; the guard keeps the heap away from that point. Fixing the library is out of scope.
- The backup reply, the settings save (both already guarded by free-memory checks), the stored
  settings text and the alert sender's outgoing message keep their strings; the CI audit lists them
  with reasons.
- The Alpaca router sizes its reply exactly; its shape and ConformU results don't change.
