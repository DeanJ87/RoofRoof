# Feature Specification: The Heap Over a Session

**Feature Branch**: `spec/045-heap-over-time`

**Created**: 2026-11-26

**Status**: Draft

## Context

Spec 038 left the board with about 61 KB as its largest free block after a fresh start. Over a
shakedown session the heap held its size but came apart (finding F40): after about 74 minutes of
a test batch (`/api/status` polled every second, a web page with its live status, N.I.N.A.
polling Alpaca, MQTT publishing, alerts, the weather polled every 5 s, some 25 moves) `heap8Bit`
read free 63 KB, largest 20 KB, lowest since start 352 B, the emergency reserve given up 8 times,
and every settings save was refused "short of memory".

On a fresh start of the same build with a headless browser on the dashboard (F40b) the largest
block fell from 61 KB to 9.7 KB within 10 minutes and `/api/status` stopped answering; closing only
the page brought it back to 32.7 KB. The web page's path (its live frames, its page loads, its own
status polls) is the main cause; the rest of the session adds slower cuts (research.md).

## User Scenarios & Testing

### User Story 1 - An open page doesn't cut up the heap (P1)

1. **Given** pages are open on the dashboard, **When** the live status goes out (every 2 s, every
   500 ms while the roof moves), **Then** the frame is written straight into one of the two
   buffers kept for frames: no 5 KB of text pieces and no copy each time.
2. **Given** a page is opened or reloaded, **Then** the browser keeps the web UI's built files
   (their names carry their content's hash) and fetches only `index.html` again.
3. **Given** a page is open with its live status, **Then** the banners on every page (the test
   box, the roof needs a person) take their state from the live status, and read `GET /api/status`
   only while the page has no live status.

### User Story 2 - What lasts doesn't land in the middle (P1)

1. **Given** alerts are sent over a session, **Then** the recent alerts (20) are kept in room
   taken once (static data), with titles, messages and per-channel details cut to fixed lengths;
   reading them copies nothing onto the heap.
2. **Given** a settings save, **Then** its strings are sized once (a doubling string asked for a
   16 KB block for 8 KB of settings), and it is refused for memory only when its own largest
   allocation (the parse of what was sent, or the settings document, or the save task's stack)
   doesn't fit; once the save task has its stack, the document is checked again and a short heap
   is answered 503, not "invalid".

### User Story 3 - The trend can be seen (P2)

1. **Given** the device has run for a while, **Then** `/api/status` `heap8Bit` gives the lowest
   largest block over the last 15 and 60 minutes, and `GET /api/diag/heap` the last hour minute by
   minute (lowest largest block, lowest free).
2. **Given** the heap is fragmented, **Then** `GET /api/diag/heap` lists the free blocks of 1 KB
   or more in address order with what is in use between each and the one before: what cut the
   largest block apart.

### User Story 4 - The soak shows a session, not a moment (P2)

1. **Given** `tools/soak/ws_soak.py --page-load S --max-drift B`, **Then** it opens the web UI
   every S seconds like a browser (index, then its files six at a time), and fails when the best
   largest block of the last 5 samples is more than B below that of the first 5.

### Edge cases

- A status frame longer than a pooled buffer (5 KB) is still sent from pieces as before.
- Both pooled buffers still being sent: the frame is skipped (counted) and a heartbeat sent, without
  first writing the document into pieces.
- A recent-alerts read while an alert is added holds the dispatcher's lock for the read (no copy).
- millis() wraps after 49 days: the trend counts minutes from the time passed, not the clock.

## Requirements

- **FR-001**: The live status broadcast MUST write into a pooled frame buffer
  (`HeapSafe::ChunkText::over`, `WebReply::pooledFrame`/`frameAll`); a new client's first frame
  too when a buffer is free.
- **FR-002**: `/assets/` MUST be served with `Cache-Control: public, max-age=31536000, immutable`;
  `index.html` and the other files keep `no-cache, no-store, must-revalidate`.
- **FR-003**: The test box and needs-a-person banners MUST use the live status when it has one
  (`useLiveStatus`) and poll `GET /api/status` at their old intervals only without it.
- **FR-004**: The recent alerts MUST be a fixed ring of fixed-size records in static data
  (`HeapSafe::FixedRing`, `HeapSafe::copyCut`: cut at a UTF-8 boundary, marked "...") and be read
  through a visitor under the dispatcher's lock.
- **FR-005**: A settings save's body string and `Config::toJson` MUST be sized once from
  `measureJson`; the memory check MUST use the save's own largest allocation
  (max(9.5 KB, body + 6.5 KB) + 2 KB, and the save task's 10 KB stack before it starts) and be
  repeated on the save task.
- **FR-006**: The main loop MUST record the heap's trend once a second (`HeapSafe::HeapTrend`, 60
  minutes, lowest largest and lowest free per minute); `/api/status` `heap8Bit.largestLow15m` and
  `largestLow60m`; the census MUST print the trend and its free-block map
  (`HeapCensus::FreeSpan`, at most 24).
- **FR-006b**: The MQTT connection's read buffer MUST be taken at its full size (4 KB) when MQTT
  starts, not grown as packets come.
- **FR-007**: No change to motion, close, park or weather behaviour; alerts change only in how the
  recent list is kept (texts cut to 64 / 192 / 32 characters).
- **FR-008**: No automatic restart for fragmentation (research R6).

## Success Criteria

- **SC-001**: With a page open on the dashboard for 10 minutes and the roof moving, the largest
  block stays at or above 20 KB and `/api/status` keeps answering (bench, the F40b repro).
- **SC-002**: The harness's hour (`ws_soak.py --page-load 30 --max-drift 12288` beside the test
  batch): no restart, largest block at or above 16 KB, drift within 12 KB, no settings save refused
  for memory (bench).
- **SC-003**: Native and web tests pass; heap budget and stack depth checks pass; flash under 91%.

## Assumptions decided for the owner

- **The save task's stack stays on the heap (not static).** Making it static costs 10 KB of RAM
  for good, taken from the region that fills first at start, so about 10 KB more of the start-up
  allocations land in the region that holds the largest free block (its largest block at rest
  falls by about that). A save's own largest allocation is the parse of what was sent (body + 6.5
  KB, about 13-15 KB), bigger than the stack, so a static stack wouldn't let a save through on a
  heap that couldn't give the parse. The "every save refused" of F40 was the fixed 20 KB rule
  against a heap that reported 20,468 B; the rule is now the save's real need.
- **Recent alerts keep 20, cut to 64 / 192 / 32 characters** (title, message, each channel's
  detail): about 8 KB of static data in place of a 4.6 KB list kept on the heap plus two strings
  per alert. A long stacked message shows its start in the recent list ("..."); the alert itself
  went out whole.
- **Hashed web UI files are cached for a year.** A firmware and UI update renames them, so a stale
  copy is never used; `index.html` is never cached.
- **No quiet automatic restart** (research R6): the causes found are bounded once fixed, and a
  restart is a visible event on a safety device.

## Out of scope

The web server library's per-connection allocations (request objects, the 2.9 KB send buffer per
round, its WebSocket queues), lwIP's buffers and PCBs, WiFi driver buffer counts, HTTPS alert jobs
and TLS (alerts go to MQTT here), a static status section pool.
