# Feature Specification: Alerts Off the Control Loop

**Feature Branch**: `spec/053-alerts-off-control-loop` | **Created**: 2026-11-27 | **Constitution**: 12.3.0 -> 12.4.0 (VI)

## Context

**Findings F55/F56 (the owner's first real session, 2026-11-27).** Pushover was switched on again at
17:45. Within the hour:

- **F55**: the device restarted (crash record 44). The log before it: "AlertQueue: Low memory
  (largest block 5364 B): alerts wait", a network write stopped, MQTT reconnecting. Record 45, 30 min
  later, the same, right after a close reached its magnet.
- **F56**: the roof latched "Control step 344 ms late > 100 ms (watchdog)": the control task hadn't
  stepped for 344 ms, so the motor watchdog stopped every move; the owner's nudges moved about 0.1 m.

Pushover has been off on the device since. Both records are reset reason 5, the **interrupt**
watchdog (300 ms): a core went without its tick interrupt for 300 ms; a 344 ms gap between control
steps is the same stall seen from the roof (research R1).

How alerts went out before this spec: the roof's control task (`roof_motion`, core 1, priority 5)
only raises flags; the main loop (core 1, priority 1) runs the outbox (spec 030), which hands one
alert a second to `AlertDispatcher::dispatch`. MQTT is published there; the HTTPS channels
(Pushover, ntfy, webhook) are queued (FreeRTOS queue of 8, a full queue failed the new alert) for
the `alerts` task (core 1, priority 1, created on demand, 8 KB). That task started a TLS session
(about 25-40 KB, spec 038) whatever the heap: the outbox's own check (6 KB block, 24 KB free, spec
035) is for handing the job over, not for TLS. On a heap at rest of 38-47 KB largest block, a
session leaves the rest of the device a few KB.

## Goal

Alerts can be on without ever affecting roof control: no TLS session on a short heap or during a
move, no alert work on the control task's core, nothing the main loop waits for. Alert behaviour
(texts, levels, critical and reminder rules, specs 005/007/030) is unchanged.

## User Scenarios & Testing

### User Story 1 - Pushover on through a night of moves (P1)

Pushover on. N.I.N.A. opens and closes the roof; alerts come (the imaging app stops checking, the
weather closes the roof).

1. An alert raised while the roof moves: MQTT at once; Pushover shows "Waiting for the roof to stop"
   under Recent alerts and is sent once the roof stops.
2. The control step's lateness stays as without alerts (`roof.maxStepGapMs` about 20-40 ms,
   `roof.watchdogTrips` 0).

### User Story 2 - Memory short (P1)

The heap is fragmented (a page load, a network return, finding F51/F52): the largest block is 30 KB.
An alert is due.

1. Pushover's send waits: "Waiting for memory"; the log, once: "pushover: Waiting for memory
   (largest block 30712 B, free 52100 B)". Checked again after 2 s, 4, 8, 16, then every 30 s.
2. Memory comes back: sent. 10 minutes without: "Waited 10 min: sent again later" (skipped for now);
   the outbox sends it again (60 s, doubling, spec 030).

### User Story 3 - A burst (P2)

Ten alerts in a minute while the alert task waits. The first eight wait; each newer one pushes out
the oldest non-critical ("Queue full", sent again by the outbox). Critical ones are sent first and
only pushed out by a newer critical one.

### Edge cases

- MQTT alerts: still published at once on the main loop's own connection (PubSubClient isn't
  thread-safe; one write on a kept connection, no new connection, no TLS).
- Plain-http ntfy or webhooks: wait for the roof the same way; their memory rule is spec 035's (6 KB
  block, 24 KB free), not the TLS floor.
- An https SQMeter's TLS session (the safety poller, same `TlsLock`) can take the heap while an
  alert waits for the lock: the floor is checked again holding the lock.
- A session already under way when a move starts goes on (it can't be cut part-way); its memory
  was checked against the floor when it started.
- The one retry after a connection error: only if the floor still holds and the roof is still.
- Test notifications (Send test) follow the same path.

## Requirements

- **FR-001**: The roof's control task MUST NOT call into the alert path (it raises flags only, as
  before). The main loop's hand-over MUST NOT wait: a fixed queue (`Alerts::SendQueue`, 8 slots)
  under a spinlock; no allocation beyond the job (spec 035's checked `new`).
- **FR-002**: Full queue: the oldest non-critical job is failed ("Queue full") to make room; if all
  are critical, a critical newcomer replaces the oldest critical, a non-critical one is failed. The
  alert task takes the oldest critical job first, else the oldest. (The outbox, spec 030, sends a
  failed alert again.)
- **FR-003**: The alert task runs at priority 1 on core 0 (the roof's control task is priority 5 on
  core 1), created on demand and stopped after 30 s idle as before.
- **FR-004**: No TLS session starts while the roof moves or below the TLS floor
  (`HeapSafe::tlsFits`: largest 8-bit block >= `TLS_MIN_LARGEST_BLOCK_BYTES` 45,056 B and free >=
  `TLS_MIN_FREE_BYTES` 57,344 B). Waits: roof moving, checked every 1 s; memory, 2 s doubling to
  30 s (`Alerts::SendGate`). After `SEND_GIVE_UP_MS` (10 min): skipped for now, "Waited 10 min: sent
  again later". The record shows the wait ("Waiting for the roof to stop", "Waiting for memory") as
  pending; the log says it once per wait.
- **FR-005**: The floor is checked again after `TlsLock` is taken.
- **FR-006**: Counted since start and shown at the end of `GET /api/diag/heap`: jobs queued, dropped,
  waits for memory, waits for a move, gave up; the alert task's least stack left; the last TLS send's
  free heap and largest block before it and free heap with the session open. Each send logs the same.
- **FR-007**: The alert task's stack (8 KB) is checked by `tools/firmware/stack_depth.py` (CI) from
  the task down to the TLS handshake, and its least-left logged when it stops.
- **FR-008**: Alert texts, levels, critical and reminder rules unchanged; MQTT unchanged.
- **FR-009**: Docs (alerts, heap budget, REST), changelog, roadmap; constitution 12.4.0 (VI).

## Constitution rules relied on

- VI (12.4.0, new bullet): alerts never touch roof control. VI: one TLS session at a time
  (`TlsLock`), outgoing HTTPS on its own task.
- I: the motor watchdog stays as it is; this spec removes a cause of its trips, not the check.
- V: the queue and the gate are pure (`lib/AlertLogic/AlertSend.h`) and tested natively.

## Tests

`test_alert_send`: oldest first; critical first; full pushes out the oldest non-critical; a critical
newcomer pushes out a non-critical first; all critical: a non-critical newcomer refused, a critical
one replaces the oldest; no allocation; order over 100,000 pushes; the TLS floor at its boundaries
and at the field's readings (F55 5.4 KB waits, F51 38.9 KB waits, spec 038 at rest goes); the gate:
goes when still with room, waits while moving (whatever the heap), backs off 2-4-8-16-30-30 s,
restarts for a new job, gives up at 10 min, texts fit a record. Simulation: a 60 s move with alerts
raised every 3 s and the heap dipping at its start: no session starts during the move, every
hand-over returns at once without allocating, no critical alert pushed out, all critical ones sent.

## Owner checks (pending)

- [ ] Bench, Pushover on (spec 053 bench note in plan.md): 10 open/close cycles with alerts raised
  during the moves (Send test during a move, the imaging app stopping): `roof.watchdogTrips` stays
  0, `roof.maxStepGapMs` stays under 60 ms, no restart; Pushover arrives after each move.
- [ ] The log's "pushover HTTP 200 ... before, ... with the session open" lines: the session's size
  (before - open) against the 40 KB the floor assumes.
- [ ] "Alert task idle: stopped (least stack left N B ...)": N above 1 KB.
