# Feature Specification: Stable Live Pages Under Load

**Feature Branch**: `spec/037-web-session-stability`

**Created**: 2026-11-25

**Status**: Draft

## Context

With several pages open on the controller (the dashboard in a few tabs, a test recorder, a
phone), the web page dropped to "Reconnecting" every 5-10 s and rebuilt itself, and the controller
restarted twice during roof moves. Spec 029's crash records (24 and 25) show panics on the
network task (`async_tcp`): `abort()` from `operator new`, called by `operator new(std::nothrow)`,
once in `AsyncWebServerRequest::create` for a new connection and once in the response's send
buffer (`AsyncAbstractResponse::write_send_buffs`). The once-a-second snapshot showed 68-82 KB
free but a largest block of 2.7-3.2 KB (about 59 KB at rest).

Findings (research.md):

1. **nothrow new aborts.** The toolchain's C++ library implements `new (std::nothrow)` by calling
   the throwing `operator new` and catching `std::bad_alloc`. The firmware is built without
   exceptions, so the throw aborts. Both crashes are this: the web server checks for `nullptr` and
   would have refused one connection.
2. **The live-update limit churned.** Past 3 clients on `/ws/status` the device closed the
   oldest. With 4 pages open each new connection knocked an older page off, which reconnected a
   second later and knocked the next one off: on the bench, 23 replacements in 4 minutes with 4
   pages, and the status poll timing out now and then. Each round allocates a request, a socket
   client, the handshake and a 4.4 KB first frame.
3. **The heap is smaller than it looked.** `maxAllocHeap` (59,380 B, never changing) and
   `freeHeap` include the IRAM heap that malloc can't use. The byte-addressable heap has about
   45 KB free at rest under load, and its lowest point since start reached 2.1 KB on the bench
   with `/api/status` at 2 Hz: several status replies (about 9 KB each with the server's buffers)
   were admitted at the same free-heap reading.
4. **Every frame was a fresh 4.4 KB block**, every 500 ms while the roof moves plus every change,
   held until each client had sent it, and a slow client could hold 4 queued frames. Mixed with
   requests' short-lived blocks, this cuts the heap into pieces. Spec 035 removed the other burst at
   move start (alerts sent together, with a TLS session).
5. **The page took a 5 s gap for a dead connection** (2.5 rest intervals), wiped what it showed
   and reconnected at once; the back-off reset as soon as a socket opened, so a page that was
   opened and closed again looped every second.

## User Scenarios & Testing

### User Story 1 - Memory running out refuses a request instead of restarting (P1)

**Acceptance Scenarios**:

1. **Given** the heap can't give a block, **When** any code uses `new (std::nothrow)`, **Then** it
   gets `nullptr` (no abort) and the web server refuses that connection or reply.
2. **Given** the largest free block is under 2 KB, **When** a connection arrives, **Then** it is
   closed before the server allocates its request.

### User Story 2 - Pages over the limit don't knock each other off (P1)

1. **Given** 3 pages have live updates, **When** a fourth opens, **Then** the fourth is closed
   with code 4429 and the three keep theirs; the fourth says "Too many pages" and tries again
   every minute or when it becomes visible.
2. **Given** a page that stopped reading, **When** its queue stays full 10 s, **Then** it is
   closed (unchanged), so a sleeping tab doesn't hold a place for ever.

### User Story 3 - The page stays readable through a gap (P1)

1. **Given** a live page, **When** no message arrives for less than 10 s, **Then** nothing
   changes.
2. **Given** the device is too short of memory for a document, **When** a broadcast is due,
   **Then** it sends `{"heartbeat":true}`: the page keeps its connection but the data ages (Open
   and Close turn off after 10 s without a reading).
3. **Given** the connection drops, **When** the page reconnects, **Then** it keeps the last
   values, greyed under a small "Reconnecting" badge, never as live (Open and Close off, Stop on).
4. **Given** sockets that open and close again without a reading, **Then** the retries back off
   (1, 2, 4, 8 ... 30 s).

### User Story 4 - Load check (P2)

1. **Given** 4 pages on `/ws/status`, `/api/status` at 2 Hz, `/api/diag` every 10 s and an Alpaca
   poll at 1 Hz with the status stream at the moving rate, **When** this runs 10 minutes, **Then**
   the device doesn't restart and its largest free block stays at or above 20 KB
   (`tools/soak/ws_soak.py`).

## Requirements

- **FR-001**: `operator new(size_t, nothrow_t)` and `new[]` MUST return `nullptr` when memory runs
  out (malloc based, `lib/HeapSafe/src/NothrowNew.cpp`).
- **FR-002**: A connection MUST be refused at accept while the largest block is under
  `HeapSafe::MIN_ACCEPT_BLOCK_BYTES` (2 KB).
- **FR-003**: A live-update client past `ConnectionBudget::WEBSOCKETS_PER_ENDPOINT` MUST be closed
  with `TOO_MANY_PAGES` (4429), from the loop task; no open client is closed for a newcomer.
  `/api/status` `connections.liveUpdates.refused` counts them (was `replaced`).
- **FR-004**: Live frames MUST come from `HeapSafe::FramePool` (2 buffers of 5 KB reserved at
  start); a frame with no free buffer is skipped. A client MUST queue at most one message
  (`WS_MAX_QUEUED_MESSAGES=1`).
- **FR-005**: A skipped status frame MUST be replaced by `ConnectionBudget::HEARTBEAT`.
- **FR-006**: The page MUST count a socket dead after `LIVE_QUIET_MS` = 10 s (5 rest intervals);
  the back-off MUST reset only on a reading; on 4429 it MUST retry every 60 s or on becoming
  visible.
- **FR-007**: The dashboard and System page MUST show the last reading after a drop, greyed with a
  badge, and treat it as not live.
- **FR-009**: At most `HeapSafe::MAX_REPLIES_IN_FLIGHT` (2) built API replies MAY be in flight;
  another non-essential `/api/` request is answered `503` busy. Alpaca, the roof's commands,
  uploads, pages and files are never held back by it.
- **FR-010**: `/api/status` MUST report `heap8Bit` (`free`, `largest`, `minFree`): the heap malloc
  uses. `freeHeap` and `maxAllocHeap` also count about 59 KB of IRAM that malloc never uses, which
  is why the largest block read about 59 KB "at rest" whatever happened.
- **FR-008**: `tools/soak/ws_soak.py` MUST run the load of User Story 4 and pass or fail on restarts
  and a largest-block floor.

## Assumptions decided for the owner

- **Refuse the newcomer, not the oldest.** The pages already open keep working; a fourth page says
  "Too many pages". A tab in a sleeping laptop still frees its place (stall close, missed
  pings). The limit stays 3.
- **10 s before a socket counts as dead**, and Open and Close stay usable for up to 10 s after the
  last reading (was 5 s). The device still refuses an unsafe move itself.
- **Last values greyed after a drop** (spec 028 showed only "Reconnecting"). They are never
  treated as live; "Connecting" still shows before the first reading.
- **A slow client misses frames** rather than queueing them; the next one is at most 2 s away.
- **10 KB is kept for frames** for the device's lifetime, instead of a fresh block per frame.
- **Two API replies at a time.** A third status or diagnostics read while two are being sent gets
  "busy, try again"; the pages retry, imaging apps are never refused by it.

## Out of scope

The web server library's own allocations beyond the nothrow forms; HTTP keep-alive; the diag
stream's cadence; Calibrate and the wizard (they keep spec 028's behaviour, with the new 10 s).
