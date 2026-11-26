# Feature Specification: A Gradual Network Return

**Feature Branch**: `spec/048-reconnect-burst`

**Created**: 2026-11-26

**Status**: Draft

## Context

Spec 045 left the heap whole at rest: on the board (build d8e625f) the largest free block of the
byte-addressable heap stayed at 37-39 KB between tests. The shakedown's network batch (finding F51,
`shakedown/runs/heap-diag-1200.txt`, research R1) then cut the access point and the N.I.N.A. PC in
turn while the roof was open, closing or waiting to close. Each time the network came back the
heap fell for a minute: the lowest largest block of that minute was 3.3 KB (the access point down
120 s while open), 4.1 KB (the access point and the N.I.N.A. PC down 121 s), 9.7 KB and 12.8 KB.
For half a minute after the first of these the device answered every request but Alpaca and the
roof's commands "busy" (its free heap under the guard's 24 KB). The deepest reading of the batch,
1.0 KB with 2.5 KB free in all, was in the first minute of an open a few minutes after the N.I.N.A.
PC came back (research R1).

What runs at a return (research R2): the clients that lost their connections come back at once
(N.I.N.A.'s Alpaca devices, the harness's 1 Hz `GET /api/status`, the pages' live status), the
safety polls catch up, MQTT reconnects after 3 s and publishes all its Home Assistant discovery
configs in one pass of the main loop, the queued alerts follow from 8 s, one a second. Each of
these was bounded on its own by earlier specs; nothing bounded them together.

## User Scenarios & Testing

### User Story 1 - Clients that come back are let in a few at a time (P1)

1. **Given** the network has just come back (the link, or the requests after 30 s of none),
   **When** clients connect, **Then** for 30 s at most 3 inbound HTTP connections are open at once,
   and past the first only while the largest block is at least 16 KB; for the next 30 s at most 4;
   then 8 (the TCP budget's share for HTTP), as before.
2. **Given** a connection over that limit, **Then** it is answered a fixed
   `503 Service Unavailable` with `Retry-After: 2` and closed as soon as it is accepted, before the
   web server allocates a request object for it.
3. **Given** the first 30 s after a return, **Then** at most one built reply (`/api/status`,
   `/api/config` and the like) is sent at once (spec 037 allowed two).

### User Story 2 - The device's own reconnect work waits its turn (P1)

1. **Given** the network has come back, **Then** MQTT connects 10 s later (was 3 s), one attempt at
   a time, 5 s apart, doubled after each failure up to 30 s; and not while the largest block is
   under 16 KB in the minute after a return (6 KB otherwise).
2. **Given** MQTT has connected, **Then** the Home Assistant discovery configs go out one at a time,
   200 ms apart, with the same memory rule, then a retained marker on `<base>/discovery` naming the
   set; **Given** a reconnect to a broker that still holds the marker of the set this boot already
   sent, **Then** nothing is sent again.
3. **Given** alerts waited for the network, **Then** they start 15 s after the link is back (was
   8 s) and go one every 5 s for the first minute (then one a second, as before). Requests coming
   back on a kept link don't hold alerts (a page opened after a quiet spell looks the same).

### User Story 3 - The trend shows the returns (P2)

1. **Given** `GET /api/diag/heap`, **Then** the trend marks each minute a network return happened in,
   and a "Network returns" section lists the last 8: when, what came back after how long, the lowest
   largest block and free heap over the minute after it (sampled ten times a second), the
   connections answered busy at accept and the most open at once, when MQTT came back, and whether
   discovery was sent again.

### User Story 4 - A soak that cuts the network (P2)

1. **Given** `tools/soak/return_soak.py --cycles 5 --down 90 --cut CMD --restore CMD`, **Then** it
   cuts the network five times while polling like the harness, reads the device's return records at
   the end, and fails on a restart, a spent emergency reserve, fewer returns recorded than cuts, or
   a largest block under 16 KB after any return.

### Edge cases

- A request object that is never freed (a stuck connection) stops counting after 10 s
  (`HeapSafe::ReplyLedger`), so the gate can't stay shut.
- An upload sets its own disconnect handler: it stops being counted (it runs for minutes anyway).
- A WebSocket counts until its upgrade; the pages' own limit (3 per endpoint) is unchanged.
- The broker lost its retained messages (restarted without persistence): no marker comes back
  within 2 s, so everything is sent again.
- The first connection after a restart, or new MQTT or discovery settings: always sent whole.
- A settings save that removes entities (discovery off, new prefix or base topic) removes them at
  once, as before.
- The roof busy (moving, a close waiting or attempting): discovery waits, as before.
- millis() wraps after 49 days: every interval is an unsigned difference.

## Requirements

- **FR-001**: `ConnectionBudget::NetworkReturn` MUST record each return: the link up again after
  being down (`LinkBack`), a client after `QUIET_RETURN_MS` (30 s) with none while the link stayed
  up (`RequestsBack`), and the first time on a network (`FirstUp`, not counted as a return); its
  phase (`Window` 30 s, `Following` until 60 s, `Settled`). `NetState::tick()` feeds it every pass.
- **FR-002**: `InboundGate::admit` MUST run in the listener's accept callback (after the IPv6 and
  2 KB checks) and apply `ConnectionBudget::inbound()`: over the limit, the fixed 503 is written,
  the connection closed and its client deleted. A taken connection's request holds an
  `InboundGate::Ticket` (in its disconnect handler) for its lifetime.
- **FR-003**: The memory guard MUST cap built replies at `repliesAllowed()` (one in a window).
- **FR-004**: `MQTTClient::reconnect` MUST use `ConnectionBudget::MqttRetry` (delay after a return,
  backoff, reset by a return) and `backgroundFits()`; a successful connect is noted in the return's
  record.
- **FR-005**: Discovery MUST go through `HaDiscovery::Pacer` (`publishOne`, `GAP_MS` 200,
  `MARKER_WAIT_MS` 2 s) and publish the retained marker (`HaDiscovery::markerOf` of the discovery
  key, 8 hex digits) after the last config; the marker is trusted only for a set this boot sent
  whole.
- **FR-006**: `AlertOutbox::handle` MUST step at `alertGapMs()` and send only when `alertsMayGo()`.
- **FR-007**: `HeapMap::upkeep` MUST sample the heap ten times a second while a return is followed,
  into the trend and the return's record; the census MUST mark the trend's minutes and print the
  "Network returns" section.
- **FR-008**: `tools/soak/return_soak.py` with tests (`test_return_soak.py`, CI).
- **FR-009**: No change to motion, close, park or weather behaviour; the safety polls are not
  delayed or capped (the safety poller is unchanged).

## Success Criteria

- **SC-001**: On the board, `return_soak.py --cycles 5 --down 90 --up 120` beside the harness's
  polling: no restart, the reserve never spent, the lowest largest block after every return at or
  above 16 KB (bench, pending).
- **SC-002**: The native model of a return (`test_return_burst`): the same arrivals that pile up
  46 KB at once under spec 038's schedule hold at most 30 KB under this one.
- **SC-003**: Native, soak tool and quality checks pass; heap budget and stack depth unchanged;
  flash within budget.

## Assumptions decided for the owner

- **Clients may be turned away for a minute after a return.** In the model about one connection
  in three is answered 503 during the first 30 s; every client involved retries (N.I.N.A.'s Alpaca
  polls, the harness, the pages). A person's Stop or Close from the web page can be answered
  "busy" once in that minute and needs a second press; the first connection is always taken, and
  the roof's own stops (faults, weather, the motion checks) don't depend on any of this.
- **Alpaca is not exempt at accept.** The path isn't known until the request is read, and reading
  it is the allocation the gate saves. Outside the minute after a return nothing changes for it.
- **MQTT waits 10 s after a return** (was 3 s). MQTT can carry the weather (spec 004): its reading
  stays stale, so unsafe, for those seconds, which only holds a closed roof closed. The safety
  polls over HTTP are not delayed.
- **Queued alerts start 15 s after a return and go one per 5 s for a minute.** They had already
  waited for the network; six alerts take about 40 s instead of 14 s.
- **Discovery is skipped only for a set this boot sent whole.** After a restart or a change of
  settings the configs always go out (paced), so a broker whose configs were deleted by hand is
  repaired by a restart.
- **The deepest reading of the batch is not explained by a return** (research R1): it came at the
  start of an open with N.I.N.A.'s park reads failing. The return records and the faster sampling
  will tell the two apart on the next run; a move-time burst is a follow-up of its own.

## Out of scope

The safety poller's timing and order (spec 047), the WiFi driver's dynamic buffers, lwIP's
send buffer, the web server library's per-connection allocations, a move's own heap use (R1),
UI changes.
