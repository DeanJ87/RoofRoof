# Feature Specification: Heap-Safe Alert Path

**Feature Branch**: `spec/035-alert-heap-safe`

**Created**: 2026-11-25

**Status**: Draft

## Context

Spec 029's crash capture recorded a panic on the main loop task (`loopTask`) 982 s after a start.
It came right after a burst of roof events: the weather turned unsafe and then safe, an Open was
pre-empted by the automatic close, the roof closed and a N.I.N.A. park request followed, with
several alerts queued. The decoded backtrace (`tools/decode_crash.py`) runs from `abort()` through
`operator new` and `std::vector<Deps::Entry>::push_back` in `Deps::evaluate`, called by
`WebServer::channelBlocks()` from the spec 030 outbox's send (`AlertQueue::step`,
`AlertOutbox::handle`, `WebServer::sendQueuedAlerts`).

The firmware has no C++ exceptions, so a failed allocation stops the device. To learn which alert
channels could be used, every send built the full settings-dependency list: 40 entries in a vector
reserved for 16 that grew to 32 and then 64 (a 1,280-byte block while the 640-byte one was still
held). The record's memory line (121 KB free, 38.9 KB largest block) is the once-a-second snapshot
taken up to a second before the crash, not the heap at the crash: by then no 1.3 KB block was
left. During a burst each send held, at the same time, its record, its MQTT text, a queued HTTPS
job carrying a copy of every alert setting (all events' wording included), the delivery task's
8 KB stack and its TLS session, while the outbox sent every due alert in the same step.

## User Scenarios & Testing

### User Story 1 - A burst of alerts doesn't restart the controller (P1)

The weather flips, the automatic close runs and the mount parks within a minute; six alerts are
queued. They go out one a second, the critical ones first, and the controller keeps running.

**Acceptance**:

1. Given eight alerts pushed into the queue (six kept), when the outbox steps for ten minutes of
   failed sends and then a delivery, then no step sends more than one alert, the critical ones go
   first, the queue empties, and the steps allocate nothing.
2. Given the alert channels' availability asked a thousand times, no allocation is made, and the
   answers equal the full settings list's for every combination of the facts and switches they
   depend on.
3. Given the heap short (largest block under 6 KB or under 24 KB free), when the outbox steps,
   nothing is sent and the alerts wait; once memory is back they go out.

### Edge cases

- A critical alert queued behind non-critical ones goes first; at most five extra seconds pass
  for the last of a full queue.
- The HTTPS job, the TLS client and the weather poll's TLS client can't be allocated: the send
  fails like a busy session and the outbox sends it again.
- The diagnostics summary list or the SQMeter sync's settings copy can't be allocated: that pass is
  skipped and tried again on a later one.

## Requirements

- **FR-001**: The alert path MUST work out the channels' availability without allocating
  (`Deps::alertChannels`), with the same answers as `Deps::evaluate`.
- **FR-002**: `Deps::evaluate` MUST reserve its exact entry count (one allocation).
- **FR-003**: The alert queue MUST reserve its capacity once and send at most one alert per step,
  a critical one first, else the oldest.
- **FR-004**: The outbox MUST NOT step while the largest free block is under 6 KB or the free heap
  under 24 KB (spec 031's limits); the alerts wait, and the log says so once per episode.
- **FR-005**: A queued HTTPS job MUST hold only the channel settings, allocated with
  `new (std::nothrow)`; a failed allocation fails the send ("Low memory"), it does not abort.
- **FR-006**: The dispatcher's records MUST be reserved once and never grow on a send.
- **FR-007**: Allocations on running paths in `src/` MUST be checked (`new (std::nothrow)`);
  `make_unique` only where a service is made at start-up. `tools/firmware/test_heap_audit.py`
  MUST enforce it, and that `Deps::evaluate` is only called by the settings route.
- **FR-008**: The weather poll MUST check its body cap before the text grows.

## Success Criteria

- **SC-001**: `test_alert_heap`: zero allocations over 600 outbox steps of a burst and over a
  thousand channel checks; one allocation per settings list.
- **SC-002**: Flash at most 90.2% of the OTA slot; zero compiler warnings.
- **SC-003**: On the bench, a burst of roof events with several alerts queued causes no restart
  (pending).

## Assumptions decided for the owner

- One alert per second: a full queue takes six seconds to go out instead of one. Critical alerts go
  first, so the delay for them is at most one second per critical alert ahead of it.
- While memory is short the outbox waits rather than dropping or sending: a send on that heap could
  abort the device and lose every queued alert that isn't kept in flash.
- The memory limits are spec 031's (6 KB block, 24 KB free), fixed in code, not settings.
- Other once-a-second copies found in the audit (the Alpaca device list, the safety snapshot, the
  Home Assistant discovery key) are bounded and small; they are left as they are.
- Not changed: what any alert says, the channels, the retry gaps, the web replies (spec 031).
