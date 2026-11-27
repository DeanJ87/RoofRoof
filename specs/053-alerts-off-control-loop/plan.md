# Plan: Alerts Off the Control Loop

**Spec**: [spec.md](spec.md) | **Research**: [research.md](research.md) | **Constitution**: 12.4.0 (I, V, VI)

## Approach

1. **The rules** (pure, `lib/AlertLogic/AlertSend.h`, `AlertSend.cpp`): `SendQueue<T, N>` (fixed
   slots, critical first, the oldest non-critical gives way) and `SendGate` (roof moving, memory
   back-off, give-up); `sendWaitText`. The TLS floor in `lib/HeapSafe/HeapGuard.h` (`tlsFits`).
2. **The hand-over** (`src/AlertDispatcher.cpp`, main loop): the FreeRTOS queue of pointers becomes
   the `SendQueue` under a spinlock with a counting semaphore to wake the task; the job carries
   `critical`; a dropped job's channels are failed "Queue full". `deliveryStats()`.
3. **The alert task** (new `src/AlertSender.cpp`, keeping `AlertDispatcher.cpp` at 300 lines): on
   core 0 at priority 1; `waitToSend` asks the gate before each channel, `mayStart` again under
   `TlsLock`; `sendMeasured` logs the heap before and with the session open; the stack's least-left
   kept and logged when the task stops.
4. **The roof check**: `WebServer` hands the dispatcher `roofMoving()` (the roof's atomics).
5. **Visibility**: the counts at the end of `GET /api/diag/heap` (a text document: no schema, no
   dashboard inventory change).
6. **Checks**: `stack_depth.py` gains the alert task; the device-message catalogue reads
   `AlertSender.cpp` and `AlertSend.cpp` (three new detail texts).
7. **Tests, docs, changelog, roadmap, constitution 12.4.0.**

## Budgets

| What | Change |
|---|---|
| Static RAM | +8 B (spinlock, counters) |
| Heap kept | none; the task's 8 KB stack is still taken only while it runs |
| Per alert | unchanged (the job, spec 035) |
| Alert task stack | 8,192 B; deepest static path 5,792 B + 1 KB margin |
| Image | 1,695,399 -> 1,698,307 B (+2.9 KB), 92.4% -> 92.6% of the 1,835,008 B OTA slot |

## Bench note for the owner

1. Flash this branch; Settings, Alerts: Pushover on, Send test (it arrives; the log shows
   "pushover HTTP 200 in N ms: heap free A B (largest B B) before, C B with the session open").
2. Run 10 open/close cycles. During each move press **Send test** once (and/or let the imaging app's
   "stopped checking" fire). Under Recent alerts the Pushover line shows "Waiting for the roof to
   stop" while moving and "HTTP 200" after.
3. After the cycles: `GET /api/status` `roof.watchdogTrips` 0 and `roof.maxStepGapMs` under about
   60 ms (it is 20-40 ms on a quiet board); no new record in `GET /api/diag/crashes`;
   `GET /api/diag/heap` ends with the alert counts (waits for a move > 0, gave up 0) and the alert
   task's least stack left (above 1 KB).
4. Leave Pushover on overnight; next morning the same three readings.

## Complexity Tracking

None: Principle VI gains a rule (MINOR); nothing is relaxed.
