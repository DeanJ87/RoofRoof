# Research: Advanced Diagnostics

## R1 Where recording runs, and why it can't change the control step

- **Decision**: `RoofControl::stepLocked()` ends with `if (diagLog != nullptr) diagLog->step(sample)`,
  after `controller.tick()`, `applyOutput()`, `syncFault()` and `refreshSnapshot()`. The sample is
  built from values the step already holds (the controller's status and output, the step's supply
  and running current, the origin of the last accepted move command, the slow and top duty).
- **Rationale**: the motor output for this step is on the pins before the recorder runs, so the
  recorder can't delay it; the fault checks have already run. The step already holds the roof's
  lock, so the recorder needs no lock of its own and no reader can see a half-updated move. The
  recorder allocates nothing, logs nothing and does no I/O: per step it updates a handful of sums,
  minima and maxima; when a profile interval ends it appends one 8-byte point; when the profile is
  full it merges 120 points into 60 (a loop of 60 iterations of four integer operations), at most
  once per doubling of the interval, so at most four or five times in the longest move.
- **Worst case**: the native test `test_step_work_bounded` runs the recorder through a move that
  merges the most it can and times the worst single step on the host; the bound in the test (50 µs
  on the host, which is tens of times faster than the ESP32 at 240 MHz for this integer work) is
  generous, and the arithmetic bound above (about 300 simple operations) is what matters: well
  under 10 µs on the ESP32 against a 20 ms period and the 100 ms watchdog limit. The bench check
  measures it on the board (R8).
- **Alternatives**: recording on the main loop from snapshots (misses steps, and the main loop runs
  only every 10 ms or slower); a lock-free ring written by the step and read by the main loop
  (more code for no gain, since the step already holds the lock and readers' copies are bounded,
  R4).

## R2 The log's memory: allocated only while on, handed over under the lock

- **Decision**: `Diagnostics` (main loop) owns the `Diag::MoveLog` (about 5.2 KB), the
  `Diag::SummaryList` (about 2 KB) and a 1 KB copy buffer (about 8.5 KB in all). When `diag.enabled` becomes true it allocates both
  (`new (std::nothrow)`); `RoofControl::attachDiag(log)` sets the step's pointer under the lock.
  When it becomes false, `RoofControl::detachDiag()` clears the pointer under the lock and returns
  it; the main loop deletes it afterwards. The control task sees the pointer only under the lock,
  so it can never use a freed log; readers also copy only under the lock.
- **Rationale**: with the mode off the heap is untouched; a fixed-size single allocation can't
  fragment over time. The roof's lock already orders the control task and every reader.
- **Alternatives**: a static buffer (always costs 7 KB of RAM); allocating on the control task
  (non-deterministic time in the step).

## R3 The profile: 250 ms points, merged pairwise when full

- **Decision**: a point is one interval's average and highest running current (centiamps), lowest
  readable supply (centivolts) and average duty (0-255), 8 bytes. The interval starts at 250 ms.
  When the 120th point is appended, points `2i` and `2i+1` merge into point `i` (average of the
  two averages, the higher maximum, the lower minimum, the average duty; a missing value on one
  side takes the other side's) and the interval doubles. The interval being filled then starts
  exactly at a boundary of the new interval (point 60 at `60 × 2T = 120 × T`), so no point ever
  straddles two intervals. The last point of a move is flushed at the move's end, shorter than
  the rest.
- **Rationale**: bounded memory with the whole move always covered; 2 to 4 points a second for a
  normal 50 s move (100 points at 500 ms), and a 3-minute move from an unknown position still fits
  (2 s points). The highest current and lowest supply per point keep short stalls and sags
  visible.
- **Alternatives**: a fixed rate with a cap (a long move is cut off); keeping every step (2,500
  steps a move; 20 KB).

## R4 Reading the log from other tasks

- **Decision**: readers copy under the roof's lock: one `Diag::Move` (about 1 KB), the held ids,
  or the live part of the log (about 60 bytes). The summary list lives on the main loop behind its
  own `std::mutex`; a handler copies it (about 2 KB) and lets go before writing the reply. Replies
  are rendered from the copy: the live document through a 2.5 KB ArduinoJson pool (summaries through a 1 KB heap pool), the list, the
  move and the CSV as a chunked response generated part by part (one summary or 8 points per
  part), so no reply needs a pool of tens of KB.
- **Rationale**: the step waits for a reader at most for a 1 KB copy, as it does today for
  `snapshot()`; FreeRTOS mutexes inherit priority, so a lower-priority reader holding the lock is
  boosted. A chunked reply keeps the handler's heap to the copy plus one part.
- **Alternatives**: rendering under the lock (holds the step for a JSON write); one large JSON pool
  (120 points × 5 values would need about 10 KB of ArduinoJson slots).

## R5 The ramp phase

- **Decision**: worked out by the recorder from the move so far: `idle` when not opening or
  closing; `deadTime` while moving and not driven; `slow` for a move from an unknown position;
  `cruise` at or above the top duty; once the duty has fallen (below the previous step's, or below
  the top duty after reaching it) `slowing`, or `creep` at or below the slow duty; otherwise
  `softStart`.
- **Rationale**: the motion profile (spec 002 research R2-R4) rises from the slow to the top duty,
  holds, slows by distance and ends at the slow duty; these names follow it without adding
  anything to the motion logic.

## R6 Supply at rest, sag and stall current

- **Decision**: while the motor isn't driven, readable supply readings are averaged in blocks of
  1 s; the last full block is the supply at rest. A move's rest value is the one current when its
  motor is first switched on (null when none has completed since start). Sag = rest − lowest
  readable supply while driven; the live sag is rest − now while driven. The last 13 driven
  current readings (260 ms) are kept in a ring; a move that ends in a fault after driving has a
  stall current = their average.
- **Rationale**: measures the sag the move causes, not a battery that is simply low; the stall
  window matches the logic's 250 ms over-current confirmation.

## R7 The live document's sources

- **Decision**: `RoofControl::diagLive()` copies under the lock: the controller's status, output and
  settings, the step's supply and running current, the logical driver outputs (EN, RPWM, LPWM), the
  drive budget used toward each end, the watchdog trips, the longest gap and oldest beat, and the
  log's live part (phase, rest volts, current and last move id). The rest is read on the handler's
  task: the step times (atomics written by the control task), the loop pass times (atomics written
  by the main loop), `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`, the largest free block,
  `uxTaskGetStackHighWaterMark` for the control task, the main loop task and `async_tcp`
  (`xTaskGetHandle`), `WiFi.RSSI()`, `millis()` and `esp_reset_reason()`. Remaining time is
  `Roof::expectedMoveMs(settings, remaining distance)` while the position is known.
- **Rationale**: every value already exists on the device; nothing new is measured except the step
  and pass times.

## R8 Measuring the step and the loop pass

- **Decision**: while a log is attached (an atomic flag set with it), `RoofControl::run()` reads
  `esp_timer_get_time()` before and after `step()` and stores the last and the longest duration in
  atomics; with no log attached it reads nothing. `Diagnostics::passStarted()/passEnded()` do the
  same with `micros()` around `loop()`'s work, only while on.
- **Rationale**: two timer reads (well under a microsecond each) per step while on; nothing while
  off. The measurements let the bench check compare the step time with the mode on against the
  20 ms period, and the loop pass against spec 008's 500 ms MQTT pass budget.

## R9 The stream

- **Decision**: a second WebSocket, `/ws/diag`, sending the live document every 250 ms while the
  roof is opening or closing and every 2 s otherwise, only while the mode is on and a client is
  connected. With the mode off a connecting client is closed. It uses the status socket's helpers:
  3 clients per endpoint (the oldest replaced), stalled clients closed after 10 s.
- **Rationale**: the shared status socket's clients treat every message as the status document;
  `ConnectionBudget` already reserves two WebSocket endpoints (SQMeter had two), so the TCP budget
  is unchanged.

## R10 MQTT

- **Decision**: `Diag::MqttSchedule` decides: nothing while busy (the roof moves, a close waits or
  the automatic close is attempting: spec 008's `roofBusyForMqtt`); at rest the live document every
  60 s and the summaries of moves finished since the last publish, one per pass, newest last. Not
  retained. A failed publish waits for the next interval (live) or the next pass (summary).
- **Rationale**: diagnostics use none of spec 008's busy budget, so they can't delay the state
  document or the safety hand-over; at rest the 1 s write budget applies as for every document.

## R11 Kept summaries

- **Decision**: `/diag-moves.bin`: a 16-byte header (magic `RRDG`, version 1, count, next id, CRC-32
  of the header's version, count and next id and of the records) and up to 32 records of 56 bytes, little-endian, written whole. `Diag::KeptWrite`
  decides when a write is due: setting on, the list changed since the last write, 5 minutes since
  the last write (or none yet), and the platform not busy; the platform check itself is
  `PlatformGuard::FlashWrite` (the roof at rest, no close waiting, the sequence idle, no committed
  restart). Read at start-up and when the setting is switched on.
- **Rationale**: at most 12 writes of 1.8 KB an hour, with LittleFS's wear levelling; spec 010's
  guard is the one place flash writes are allowed.

## R12 The demo

- **Decision**: `DemoRoof::EmulatedRoof` owns a `Diag::MoveLog` while `diag.enabled` is on and steps
  it in its 20 ms step after the output is applied, exactly as the firmware. `EmulatedDevice` keeps
  the summary list and writes the same documents through `lib/DiagLogic`, with the device-only
  values null and `simulation: "demo"`. MSW serves the routes and links `/ws/diag`.

## R13 A read interface for a later calibration run (spec 016)

- **Decision**: `RoofControl::diagMove(id, Diag::Move &)` and `diagLatestId()` are public, and
  `Diag::Move` carries the summary and the profile in physical units through accessors
  (`Diag::pointAmps`, `Diag::pointVolts`, `Diag::pointTimeMs`). A calibration run can read a move
  without the web documents. Nothing here depends on spec 016.
