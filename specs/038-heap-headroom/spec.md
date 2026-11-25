# Feature Specification: Heap Headroom

**Feature Branch**: `spec/038-heap-headroom`

**Created**: 2026-11-25

**Status**: Draft

## Context

Spec 037 made a heap that runs out survivable for the allocations that are checked (`new
(std::nothrow)` returns null, replies are capped and answered "busy"), and exposed the real
figure: `heap8Bit`, the byte-addressable heap ordinary allocations use. That figure is small. At
rest the largest free block is about 21-39 KB; under spec 037's soak (4 live pages, `/api/status`
at 2 Hz, Alpaca at 1 Hz) it fell to 8-10 KB and the lowest free since start to 584 B.

The board still restarted after 037 (crash record 26, 220 s after a restart under the shakedown
recorder, N.I.N.A. polling and a page or two): `abort()` on the network task (`async_tcp`) with a
largest block of 10.7 KB. Every allocation that isn't the nothrow form still aborts when the heap
can't give it: the C++ library's throwing `operator new`, used by `std::string`, `std::list` nodes
and `std::function` inside the web server, has no exceptions to throw, and a new connection's
request parsing does several of those.

What takes the heap (research.md R1-R3): about 61 KB of static data; task stacks sized with large
margins (loop 12 KB with a 7.3 KB peak, `async_tcp` 11 KB with a 5 KB peak, the timer task 8 KB for
callbacks a few hundred bytes deep); per-connection send buffers of up to 5.7 KB of unacknowledged
data each; and the buffers spec 037 keeps (2 x 5 KB frames, the MQTT buffer).

## User Scenarios & Testing

### User Story 1 - The device survives running out (P1)

1. **Given** the heap can't give a block, **When** code anywhere uses the throwing `operator new`,
   **Then** a 2 KB reserve is given up and the allocation retried, instead of `abort()`; only a
   second failure before the reserve is back aborts.
2. **Given** the reserve is spent, **When** a non-essential request arrives, **Then** it is
   answered "busy" as when the heap is short (Alpaca, the roof's commands and uploads still run),
   until the main loop takes the reserve back (largest block at least 8 KB); the use is logged and
   counted (`heap8Bit.reserveSpent`).

### User Story 2 - More of the heap is free (P1)

1. **Given** the release firmware at rest (no pages, nothing polling), **When** it has run a few
   minutes, **Then** the largest free block is at least 40 KB.
2. **Given** spec 037's soak (4 pages, `/api/status` at 2 Hz, `/api/diag` every 10 s, Alpaca at
   1 Hz) for 10 minutes, **Then** the device doesn't restart, the largest free block stays at or
   above 20 KB and the free heap at or above 15 KB.

### User Story 3 - Where the heap went (P2)

1. **Given** a person or a tool asks `GET /api/diag/heap`, **Then** it gets, as text, the totals,
   each heap region, blocks in use and free by size, the 16 largest blocks in use (a task's stack
   named) and each task's stack block and least stack left.
2. **Given** the roof moves or a close waits, **Then** the census is refused (409): the walk holds
   the heap's lock.

### User Story 4 - The savings stay (P2)

1. **Given** a change that grows a task stack, a kept buffer or the static data past the recorded
   budget, **When** CI builds it, **Then** the heap budget check fails, until the growth is recorded
   with a reason.
2. **Given** the board on the bench, **When** `tools/soak/ws_soak.py --clients 0 --also --floor
   40960` runs at rest, **Then** it fails if the largest block drops below 40 KB.

### User Story 5 - A network return isn't a heap cliff (P1)

1. **Given** the roof's network was down 2 minutes with alerts queued, **When** it comes back,
   **Then** the safety polls resume at once, MQTT reconnects after 3 s and the queued alerts go
   from 8 s, so their TLS sessions don't coincide with MQTT's discovery burst.

### Edge cases

- A failure inside the heap walk's callback can't happen: it only counts into fixed arrays.
- A second `operator new` failure while the reserve is spent aborts as before (no worse).
- The reserve is retaken only from the main loop, never from the new-handler.

## Requirements

- **FR-001**: `GET /api/diag/heap` MUST return the heap census as plain text
  (`HeapSafe::HeapCensus`, nothing allocated during the walk), need the password when protection is
  on, answer 409 while the roof moves or a close waits and 503 when its working memory can't be had
  or another census runs.
- **FR-002**: The task stacks MUST be: loop 10 KB, `async_tcp` 8 KB, `esp_timer` 4 KB (were 12, 11
  and 8 KB); roof (6 KB), `safetyPoll` (8 KB, room for a TLS handshake) and `alerts` (8 KB, started
  on demand) unchanged.
- **FR-003**: A 2 KB reserve (`HeapSafe::HeapReserve`) MUST be taken at start and given up by the
  C++ new-handler when `operator new` fails; the memory guard MUST treat a spent reserve as a short
  heap; the main loop MUST take it back once the largest block is at least 8 KB and log each use;
  `/api/status` `heap8Bit.reserveSpent` MUST count the uses.
- **FR-004**: The TCP send buffer stays at 4 segments (5,744 B): halving it was tried on the
  bench and dropped (research R5).
- **FR-005**: `tools/firmware/heap_budget.py` MUST sum the kept stacks and buffers (read from the
  sources) and the static data (from the `.elf`) and fail CI when the sum passes
  `tools/firmware/heap-budget.json`.
- **FR-006**: `tools/soak/ws_soak.py` MUST also fail on a free-heap floor (`--free-floor`) and run
  with no pages (`--clients 0`) for the at-rest check.
- **FR-007**: No change to motion, close, park or weather behaviour; alerts change only in when a
  queued one is sent after the network returns (FR-008).
- **FR-008**: After the network comes back (or first comes up) the work that reconnects MUST be
  staggered: the safety polls at once, MQTT (with its discovery documents) after
  `ConnectionBudget::MQTT_SETTLE_MS` (3 s), queued alerts after `ALERT_SETTLE_MS` (8 s) of the
  network being up (`ConnectionBudget::NetworkSettle`, fed by the main loop).

## Success Criteria

- **SC-001**: At rest, largest free block at least 40 KB (bench).
- **SC-002**: Spec 037's soak for 10 minutes: no restart, largest block at least 20 KB, free heap at
  least 15 KB (bench).
- **SC-004**: After a 2-minute network outage at rest, `heap8Bit.minFree` stays above 8 KB and no
  reply is refused (bench, network blocked at the access point).
- **SC-003**: Flash stays under 91% of the app slot; LittleFS and the UI unchanged.

## Assumptions decided for the owner

- **A 2 KB emergency reserve** is kept back (the budget pays for it out of the stack savings): one
  out-of-memory moment costs a few "busy" replies instead of a restart. A second one before the
  heap recovers still restarts, as today.
- **Smaller stacks with about 3 KB spare each** (loop 2.9 KB, `async_tcp` 3.2 KB over the peaks the
  crash snapshots recorded; the timer task's callbacks need well under 2 KB). A stack overflow is
  caught by FreeRTOS and restarts the device with a record; the census shows each task's least
  stack left so the margin can be watched.
- **The TCP send buffer stays at 4 segments.** Halving it was tried on the bench; MQTT writes
  then stalled and dropped the connection after a network return (research R5), and MQTT can
  carry the weather, so it was put back.
- **Heap poisoning stays on** (light: about 8-12 B per block) because it turns a buffer overrun
  into a recorded crash rather than silent corruption.
- **Reconnect work staggered** after a network return: MQTT 3 s later (its weather and commands
  stay stale, so unsafe, for those seconds; Stop is also on the page and Alpaca), queued alerts
  8 s later (they had already waited for the network). The safety polls are not delayed.
- **Not taken (research R4)**: compiling IPv6 out (it is a setting, off by default, and costs no
  memory per IPv4 connection); fewer WiFi buffers or no AMPDU (the framework sets the buffer
  counts at run time, and each needs its own reconnect and throughput soak); smaller TLS buffers
  (dynamic and asymmetric already; servers without fragment negotiation need 16 KB in);
  starting the alert task at boot (8 KB held for good instead of on demand).

## Out of scope

The web server library's own allocations; rewriting `GET /api/config` in sections (its 2 x 9.7 KB
blocks are transient and guarded); WiFi driver buffer counts; TLS settings; UI changes.
