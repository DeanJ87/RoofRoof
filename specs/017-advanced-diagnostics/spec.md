# Feature Specification: Advanced Diagnostics

**Feature Branch**: `spec/017-diagnostics`

**Created**: 2026-11-11

**Status**: Draft

## Context

The status document already shows what the roof is doing: its state, the estimated position,
the Hall reading, the supply volts, the motor current when current sense is fitted, the latched
fault and the control task's worst step gap ([REST API](../../docs/api/rest.md)). That is
enough to run the roof, but not to understand how it is wearing. A roller that starts to bind,
a motor drawing more current each month, a supply that sags further under load, or a move that
takes a little longer every week shows up only as numbers during a move, and today those
numbers are gone as soon as the move ends.

This spec adds an **advanced diagnostics mode**, off by default. When the owner switches it on,
the device:

- publishes a **live diagnostics document** with everything it can measure: the supply and its
  sag under load, the motor current and power, the duty, direction and ramp phase, the driver's
  enable and PWM levels, the Hall reading, the position estimate with the elapsed and remaining
  time and the timeout, the drive budget used, the control task's step time and worst gap, the
  watchdog margin, the main loop's pass time, the memory and stack headroom, the Wi-Fi signal,
  the uptime and the reset reason;
- streams that document over a WebSocket, four times a second while the roof moves;
- keeps a **log of the last moves** in memory, each with its summary (when, who asked, where to,
  how it ended, how long it took, the peak, average and stall current, the lowest supply, the
  Hall reading at each end) and a downsampled profile of the current, the supply and the duty;
- optionally keeps the summaries of the last 32 moves across restarts, written to the
  filesystem only while the roof is at rest, at most once every five minutes;
- optionally publishes the live document and each move's summary over MQTT, only while the roof
  is at rest;
- shows a **Diagnostics page** with live tiles, a chart of the current and supply during a move,
  and the move log with each move's chart and a CSV or JSON download.

When the mode is off, none of this is computed, sent or stored: the control task, the main
loop and MQTT run exactly as today.

Diagnostics observe; they never decide. Nothing here feeds the motion logic, the safety verdict,
the close sequence or an alert, and no diagnostics value can stop, start or slow the roof.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: How many moves does the in-memory log keep, and how fine is each profile? → A: The last 5
  moves (the one in progress included), each with up to 120 profile points. A point covers 250 ms
  (4 points a second) to start with; when a long move fills the profile, neighbouring points are
  merged and each point then covers twice as long, so a whole move always fits. A normal 50 s
  move ends with 100 points at 2 a second; the longest move a timeout allows still fits. The log
  is about 8.5 KB with its summary list and one copy buffer, taken from the heap only while the mode is on.
- Q: What does each profile point hold? → A: The average and the highest motor current in its
  interval (when current sense is fitted), the lowest supply volts and the average duty. The
  lowest supply and the highest current are what show a stall or a sagging supply; an average
  would hide a short spike.
- Q: What does "stall current" mean in a move's summary? → A: For a move that ended in a fault
  while the motor was driven, the average current over the last 250 ms of driving (what the motor
  drew as it stopped); null for any other ending, and null without current sense.
- Q: How is the supply's sag measured? → A: Against the supply at rest: the average of a full second of
  readings with the motor off, the latest one completed before the motor was switched on for the
  move. The sag is that rest value
  minus the lowest reading while driving. The live document also shows the sag now while the motor
  runs.
- Q: Is the move log kept across a restart? → A: Only the summaries, and only with the separate
  "Keep move summaries" setting on (off by default). The last 32 summaries are written to one
  file of about 2 KB, as a whole, at most once every 5 minutes, and only while the roof is at
  rest, no close waits and the close sequence is idle (spec 010's rules for a flash write). The
  profiles stay in memory.
- Q: What does MQTT publish, and when? → A: With the separate "Diagnostics over MQTT" setting on
  (off by default): the live document on `<base>/diag` every 60 s while the roof is at rest, and
  each finished move's summary on `<base>/diag/move` once the roof is at rest again. Nothing at
  all is published on these topics while the roof moves, a close waits or the automatic close is
  attempting, so spec 008's busy budget is never used by diagnostics. Neither topic is retained.
- Q: Where is the Diagnostics page, and how is it hidden? → A: A page of its own at
  `/diagnostics`, reached from a Diagnostics card on the System page. The card and the page's
  content appear only while the mode is on; with it off, the page says the mode is off and links
  the setting. It is not in the main navigation (constitution VII).
- Q: Who may clear the move log? → A: A person, with the password when protection is on and the
  `X-RoofRoof: 1` header, and not while the roof moves or a close waits (the move in progress
  would be lost). Clearing also removes the kept summaries, as a flash write by spec 010's rules.
- Q: Who may read the diagnostics? → A: Like `/api/status`: readings only, no address, no
  secret, nothing changed. The device's free memory, stack headroom and reset reason are already
  in the status document; the log adds when the roof moved and who asked, which the status and
  the alerts already reveal on a trusted LAN (constitution VIII).
- Q: How is it shown that the control task's timing doesn't change? → A: Three ways. The
  recording runs after the step has applied the motor output, so it can't delay the output; its
  work per step is bounded and allocation-free, proved by native tests; and with the mode on the
  device measures each step's own duration, so the bench check can compare it with the 20 ms
  period and the 100 ms watchdog limit, with the mode on and off.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | Motion control runs in its own task at a fixed rate; a missed deadline stops the motor | FR-020 to FR-023: recording runs inside the step after the output is applied, does a bounded amount of work, allocates nothing and takes no lock beyond the one the step already holds; with the mode off it is a single check |
| I | OTA, restarts and flash writes wait while the roof moves | FR-016, FR-017: kept summaries are written only through spec 010's flash-write guard, never while the roof moves, a close waits or the sequence is busy |
| III | Alerts, MQTT and the UI report; they never change the verdict or the motion decisions | FR-024: diagnostics feed nothing back into the motion logic, the gates, the close sequence or the alerts |
| VI | Request handlers don't block on the network or hardware; heap and stack measured | FR-012, FR-025, FR-026: handlers copy under the roof's lock (a bounded copy) or a short mutex; the log is sized and allocated only while on |
| VII | Quiet UI; Stop on every page while moving | FR-030 to FR-034: hidden unless enabled, under the same Stop bar, existing cards and tiles |
| VIII | State-changing endpoints call `requireAuth` | FR-014: clearing the log needs the password and the command header; every other endpoint only reads |
| IX | Docs and OpenAPI move with the change | FR-036 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Watch a move as it happens (Priority: P1)

The owner suspects the roof is binding near the open end. They switch on advanced diagnostics in
Settings → Device, open the Diagnostics page from the System page and press Open. While the roof
moves, the page shows the supply and how far it has sagged, the motor current and power, the
duty and the ramp phase, the position estimate with the time left and the timeout, the drive
budget used, and a chart of the current and supply drawing itself four times a second.

**Why this priority**: Seeing the load during a move is the reason for the feature.

**Independent Test**: In the demo, switch the mode on, open the Diagnostics page and open the
roof; the tiles change during the move and the chart gains about four points a second.

**Acceptance Scenarios**:

1. **Given** the mode is on and a client is connected to the diagnostics stream, **When** the
   roof moves, **Then** the client receives the live document about every 250 ms, and about every
   2 s at rest.
2. **Given** a move with current sense fitted, **When** the motor runs, **Then** the live document
   shows the motor current and the power (supply volts × current); without current sense both
   are null and the page says current sense isn't fitted.
3. **Given** the roof is opening, **When** the duty rises from the slow duty, holds at the top duty
   and falls again near the end, **Then** the ramp phase reads `softStart`, `cruise` and `slowing`
   in turn; during the dead time it reads `deadTime`, and a move from an unknown position reads
   `slow`.
4. **Given** the mode is on, **When** `GET /api/diag` is read at any time, **Then** it returns the
   same document as the stream.

---

### User Story 2 - Compare recent moves (Priority: P1)

A week later the owner wants to know whether the binding is getting worse. The Diagnostics page
lists the last moves with when they ran, who asked, the result, the duration, the peak and
average current and the lowest supply. Opening one shows its profile chart; the owner downloads
it as CSV for a spreadsheet.

**Why this priority**: Wear shows only by comparison; the log is what makes the live numbers
useful later.

**Independent Test**: In the demo, open and close the roof twice, then check that the page lists
four moves with their summaries, each opens with a chart, and the CSV download has one row per
profile point.

**Acceptance Scenarios**:

1. **Given** the mode is on, **When** a move ends (arrived, stopped or faulted), **Then** the log
   holds its summary and profile, and `GET /api/diag/moves` lists it first.
2. **Given** five moves in the log, **When** a sixth starts, **Then** the oldest one's profile is
   dropped; its summary stays in the summary list (up to 32).
3. **Given** a move in the log, **When** `GET /api/diag/moves/{id}?format=csv` is read, **Then**
   the reply is a CSV file with a header row and one row per profile point (time from the start,
   average and highest current, lowest supply, duty).
4. **Given** a move still in progress, **When** it is read, **Then** it is returned with
   `active: true` and its profile so far.
5. **Given** a move that ended in a fault while driving, **When** its summary is read, **Then** it
   names the fault with its measured value and limit, and gives the stall current.

---

### User Story 3 - The mode is off and changes nothing (Priority: P1)

An owner who never touches the setting gets exactly today's device: the same control-task
timing, the same main loop, the same MQTT traffic and no extra memory used.

**Why this priority**: The mode watches the safety-critical control task; it must not cost
anything while unused (constitution I, VI).

**Independent Test**: With the mode off, `GET /api/diag` answers `{"enabled": false}`, the
diagnostics stream closes at once, no diagnostics topic is published and no file is written;
native tests show the control step with no log attached does nothing beyond one check.

**Acceptance Scenarios**:

1. **Given** the mode is off, **When** any diagnostics endpoint is read, **Then** it answers that
   the mode is off and holds no data.
2. **Given** the mode is switched off while on, **When** the setting is saved, **Then** the log's
   memory is released and nothing more is recorded, streamed or published.
3. **Given** the mode is switched on during a move, **When** the setting is saved, **Then** the
   recording starts with the next move (a move already under way isn't recorded part-way).

---

### User Story 4 - Keep the summaries across restarts (Priority: P2)

The owner wants a longer record than five moves that survives an update. They switch on "Keep
move summaries". After a firmware update, the move list still shows the last summaries, marked
as kept (no profile).

**Why this priority**: Useful for trends over weeks; the in-memory log already covers a session.

**Independent Test**: In native tests, a summary list encoded and decoded gives back the same
summaries; a damaged file is ignored; the write decision is never due while the roof moves, a
close waits or the sequence is busy, nor within 5 minutes of the last write.

**Acceptance Scenarios**:

1. **Given** the setting is on and a move ended, **When** the roof is at rest and 5 minutes have
   passed since the last write, **Then** the summaries are written once.
2. **Given** the roof moves, a close waits or the close sequence is busy, **When** a write would
   be due, **Then** it waits.
3. **Given** a kept file from before a restart, **When** the device starts with the setting on,
   **Then** the list shows those summaries and new moves continue the numbering.

---

### User Story 5 - Diagnostics over MQTT (Priority: P3)

The owner logs everything in Home Assistant's recorder or InfluxDB. With "Diagnostics over MQTT"
on, the device publishes its live document once a minute and each move's summary after the move.

**Why this priority**: Optional; the web page and the REST API cover the need on their own.

**Independent Test**: Native tests of the schedule: nothing is due while the roof is busy; at
rest the live document is due every 60 s and a finished move's summary once.

**Acceptance Scenarios**:

1. **Given** MQTT connected and the setting on, **When** the roof is at rest, **Then** `<base>/diag`
   is published every 60 s.
2. **Given** a move ended, **When** the roof is at rest again, **Then** its summary is published
   once on `<base>/diag/move`.
3. **Given** the roof moves, a close waits or the automatic close is attempting, **Then** nothing
   is published on either topic.

---

### User Story 6 - The demo shows it (Priority: P2)

A visitor to the demo can switch the mode on and watch the simulated roof's telemetry and log,
from the same code as the device.

**Independent Test**: The demo's Diagnostics page shows the simulated roof's current, supply and
duty during a move, and the log after it; the documents match their contract schemas.

**Acceptance Scenarios**:

1. **Given** the demo with the mode on, **When** the simulated roof moves, **Then** the page shows
   the same fields as on the device, with the device-only ones (step time, heap, stacks, signal)
   shown as not available.
2. **Given** the demo's jam injector, **When** a move faults, **Then** the log shows the fault.

### Edge Cases

- **A move that never drives** (refused, or stopped in its dead time): it is recorded with a
  duration and no driving; current and sag are null.
- **No current sense**: current, power, peak, average and stall current are null, never 0.
- **An unreadable supply**: the reading is null in the live document and skipped in the profile
  and the minimum; a profile point with no readable supply shows null.
- **A move longer than the profile allows at 250 ms**: points merge pairwise, so the profile
  always covers the whole move; the interval doubles each time.
- **The clock isn't set**: start and end times are given as uptime only; the epoch fields are 0.
- **The mode switched off mid-move**: recording stops at once and the log is freed by the main
  loop; nothing half-written is kept.
- **Memory can't be allocated** when the mode is switched on: the device logs it, the documents
  say `enabled: true` with `log: "unavailable"`, and nothing else changes.
- **Many browsers**: the stream follows the existing WebSocket limit (3 per endpoint; the oldest
  is replaced) and closes a client that stops reading.
- **A kept file that is damaged or from another version**: ignored and replaced at the next write.
- **The move counter after a restart**: continues after the highest kept id, so ids never repeat
  while kept summaries exist.

## Requirements *(mandatory)*

### Functional Requirements

**Settings**

- **FR-001**: The device MUST keep three settings: `diag.enabled` (advanced diagnostics),
  `diag.keepSummaries` (keep move summaries across restarts) and `diag.mqtt` (diagnostics over
  MQTT), all off by default. Stored settings without them MUST load with all three off. They
  change no motion setting and need no restart.
- **FR-002**: `diag.keepSummaries` and `diag.mqtt` MUST have no effect while `diag.enabled` is off,
  and the settings page MUST show why (the settings-dependency catalogue).

**Live telemetry**

- **FR-003**: With the mode on, `GET /api/diag` MUST return the live document: the roof (state,
  target, ramp phase, whether the driver is on, direction, duty as 0-255 and %, EN, RPWM and LPWM
  levels, supply volts, supply at rest, sag now, whether current sense is fitted, motor current,
  power, Hall counts and level, whether the position is known, the position estimate, elapsed
  time, expected time remaining, timeout, drive budget used and its limit), the control task
  (period, last and longest step time, longest gap between steps, watchdog limit and margin,
  oldest watchdog heartbeat, watchdog trips), the main loop (last and longest pass time), memory
  (free heap, lowest free heap since start, largest free block, unused stack of the control task,
  the main loop and the network task), Wi-Fi signal, uptime, reset reason, and the id of the
  current or last move.
- **FR-004**: Values that can't be read MUST be null, never a made-up number (the existing
  status rule): current without current sense, power without current or supply, remaining time
  with the position unknown, device-only values in the demo.
- **FR-005**: With the mode off, every diagnostics endpoint MUST answer `{"enabled": false}`
  (moves: `404` for a move id) and compute nothing else.
- **FR-006**: With the mode on, the device MUST stream the live document to clients of the
  diagnostics WebSocket every 250 ms while the roof is opening or closing and every 2 s
  otherwise, within the existing WebSocket limits (3 clients per endpoint, stalled clients
  closed). With the mode off a client MUST be closed on connect.
- **FR-007**: The ramp phase MUST be one of `idle`, `deadTime`, `slow` (a move from an unknown
  position), `softStart`, `cruise`, `slowing` and `creep` (at the slow duty after slowing), worked
  out from the duty, the slow and top duty settings and the move so far.

**Per-move log**

- **FR-008**: With the mode on, the device MUST record every move that starts after the mode is on:
  from the step its state becomes opening or closing to the step it stops being either.
- **FR-009**: Each move's summary MUST hold its id, start and end (uptime ms; epoch seconds when the
  clock is set), origin (person, automatic, Alpaca client, MQTT), target, end state, result
  (`arrived`, `stopped` or `fault`) with the fault's name, measured value and limit, duration,
  driven time, whether it started from an unknown position, peak and average current while
  driven, stall current (Clarification 3), supply at rest, lowest supply while driven, sag, and
  the Hall counts at start and end.
- **FR-010**: Each move MUST have a profile of at most 120 points. A point covers 250 ms at first;
  when the profile is full, neighbouring points MUST be merged pairwise (average of the averages,
  highest of the highest, lowest of the lowest, average of the duties) and the interval doubled,
  so the profile always covers the whole move.
- **FR-011**: The log MUST keep the last 5 moves with their profiles (the move in progress counts)
  and the last 32 summaries, newest first.
- **FR-012**: `GET /api/diag/moves` MUST list the summaries, each saying whether its profile is still
  held; `GET /api/diag/moves/{id}` MUST return one move with its summary and profile (`404` when
  unknown); `?format=csv` MUST return the profile as CSV (`text/csv`, a header row, one row per
  point, offered as a download).
- **FR-013**: A move in progress MUST be readable, marked `active`, with its profile so far.
- **FR-014**: `POST /api/diag/moves/clear` MUST need `requireAuth` and the `X-RoofRoof: 1` header,
  MUST be refused (`409`, with the reason) while the roof moves or a close waits, and MUST empty the
  log and the summary list, and empty the kept file (or remove it while "Keep move summaries" is
  off) as a flash write (FR-016).
- **FR-015**: Move ids MUST increase by one per move; after a restart with kept summaries they MUST
  continue after the highest kept id.

**Kept summaries**

- **FR-016**: With `diag.keepSummaries` on, the device MUST write the summary list (at most 32, one
  file, at most 2 KB) only while the roof is at rest, no close waits, the close sequence is idle and
  no restart is committed (spec 010's flash-write guard), at most once every 5 minutes, and only when
  a summary was added or the list cleared since the last write.
- **FR-017**: The file MUST carry a version and a checksum; a file that doesn't match MUST be ignored
  (and replaced at the next write). It MUST be read only at start-up or when the mode is switched on, on
  the main loop.

**MQTT**

- **FR-018**: With `diag.enabled` and `diag.mqtt` on and MQTT connected, the device MUST publish the
  live document on `<base>/diag` every 60 s and each finished move's summary on `<base>/diag/move`
  once, both not retained, and only while the roof isn't busy (not moving, no close waiting, the
  automatic close not attempting). While busy nothing MUST be published on them.
- **FR-019**: A diagnostics publish MUST use the same publish path, socket time limits and write
  budget as the other documents; a failed live document waits for the next interval, a failed
  summary goes again at the next check (once a second, at rest only).

**Timing and memory**

- **FR-020**: Recording MUST run in the control task's step, under the lock the step already holds,
  after the step has applied the motor output, and MUST NOT allocate memory, wait, log, write flash
  or call the network.
- **FR-021**: The work per step MUST be bounded: constant per step, plus at most one merge of the
  profile (120 points) when it fills.
- **FR-022**: With the mode off, the control step MUST do nothing for diagnostics beyond checking
  that no log is attached, and MUST NOT measure its own duration.
- **FR-023**: With the mode on, the control task MUST measure each step's duration (start of the
  step to its end) and keep the last and the longest; the main loop MUST measure each pass's duration
  likewise. These are reported only (FR-024).
- **FR-024**: No diagnostics value MUST feed the motion logic, the gates, the safety verdict, the
  close sequence, the alerts or any decision; diagnostics only copy what those produce.
- **FR-025**: The log MUST be taken from the heap when the mode is switched on and given back when it
  is switched off, in pieces of a fixed size (about 8.5 KB in all), by the main loop; the control task
  only sees it attached or not, under the roof's lock.
- **FR-026**: Reading the log from another task MUST be a bounded copy under the roof's lock (one
  move at most, about 1 KB) or the summary list's own short lock; no reader holds a lock while
  writing a reply.
- **FR-027**: The main loop's diagnostics work MUST be bounded: at most one stream document every
  250 ms, one summary copy per finished move, and the MQTT and file writes above.

**Demo and bench**

- **FR-028**: The demo's simulated roof MUST feed the same recorder, so the demo serves the same
  documents and stream; device-only values are null there.
- **FR-029**: Bench firmware MUST record its simulated moves like real ones.

**Web UI**

- **FR-030**: Settings → Device MUST have a Diagnostics card with the three settings and a "?" for
  each, the two dependent ones showing why they have no effect while the mode is off.
- **FR-031**: With the mode on, the System page MUST show a Diagnostics card with a few live values
  and a link to the Diagnostics page; with it off, the card MUST NOT appear.
- **FR-032**: The Diagnostics page MUST show live tiles for the roof, the control task, the main
  loop, memory and the network; a chart of the current and supply (and the duty) during the move in
  progress or the last one; the move list; a chosen move's chart and summary with CSV and JSON
  downloads; and Clear log, with its reason when it is unavailable.
- **FR-033**: With the mode off, the Diagnostics page MUST say so and link the setting, and open no
  stream.
- **FR-034**: The chart MUST follow the project's chart guidance: two series at most per axis, the
  units on the axes, a legend, readable in light and dark, and a text summary for screen readers.

**Interfaces and docs**

- **FR-035**: The routes MUST be in the route registry and the OpenAPI document, the documents MUST
  have contract schemas, and the demo's documents MUST match them.
- **FR-036**: The user guide, the REST, WebSocket and MQTT references and the configuration
  reference MUST describe the mode, its documents and its limits.
- **FR-037**: The recorder MUST offer a read interface for one move's samples (summary and profile)
  that other firmware code can use, so a later calibration run can read a move's profile without
  depending on the web documents.

### Key Entities

- **Live document**: the readings of one moment, the roof's and the device's.
- **Move**: a summary and a profile, identified by an id.
- **Move summary**: when, who, where to, how it ended, and the load figures.
- **Profile point**: one interval's average and highest current, lowest supply and average duty.
- **Kept summaries**: the last 32 summaries in one versioned, checksummed file.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (each group of values in a `Card`), DS-02 (the move result as a pill),
  DS-03 (values as `ReadingRow`s and `MetricTile`s), DS-04 (explanations behind "?"), DS-06 (a
  Clear's outcome in its card), DS-09 (phone width: tiles wrap, the chart scales), DS-20 to DS-28
  (copy). No new button or card styles (constitution VII); the chart uses the existing tokens and
  the SVG helpers.
- **Mockup**: not reviewed. The owner was not available; the page is built from existing cards,
  tiles and rows. The owner reviews it in the demo before merging (a pending check in the PR).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With the mode on, an owner can see the current, supply, duty and phase update at least
  three times a second during a move, and open any of the last five moves' charts in two clicks
  from the System page.
- **SC-002**: With the mode off, the control step's diagnostics work is one check (native test with
  no log attached), the documents hold no data, nothing is streamed or published and no file is
  written (tests for each).
- **SC-003**: The recorder's work per step is bounded and allocation-free (native tests count
  allocations and bound the worst step); recording happens after the output is applied (code order,
  reviewed), and the bench check shows the longest step stays far below the 20 ms period with the
  mode on.
- **SC-004**: A 50 s move's profile has between 80 and 120 points and covers the whole move; a move
  up to the longest timeout still fits in 120 points (native tests).
- **SC-005**: The log uses no heap with the mode off; with it on, the log and the summary list
  take at most 8 KB (a native test of their size), plus one 1 KB copy buffer.
- **SC-006**: A kept file survives a restart and a damaged one is ignored (native tests); no write is
  due while the roof is busy, or twice within 5 minutes (native tests).
- **SC-007**: No diagnostics MQTT publish is due while the roof is busy (native tests of the
  schedule).
- **SC-008**: The demo's documents match their contract schemas; the full local check (firmware
  build, native tests, web tests, Playwright with the accessibility scan, quality gate, docs build,
  flash and UI budgets) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Off by default, and two separate switches** for keeping summaries and for MQTT, both doing
   nothing while the mode is off. Nothing changes for an owner who doesn't ask for it.
2. **Five moves with profiles, 32 summaries.** About 8.5 KB of heap, taken only while on: a whole
   evening's opening and closing with charts, and weeks of summaries, without coming near the heap
   the alert sender's TLS session needs (about 40 KB).
3. **A profile point every 250 ms, merging when full**, so every move fits in 120 points (2 to 4 a
   second for a normal move); the lowest supply and highest current are kept per point so short
   events aren't averaged away.
4. **Only summaries are kept across restarts**, at most once every 5 minutes, about 2 KB a time,
   and only through spec 010's flash-write guard. Profiles would cost 30 times the writes for data
   that matters mostly during the session.
5. **MQTT publishes nothing while the roof is busy**, rather than a rate-limited stream: the
   tightest reading of spec 008's budget, and diagnostics never compete with the roof state
   document or delay the safety hand-over.
6. **The stream is its own WebSocket** (`/ws/diag`), so the status socket's clients aren't sent a
   different document; it fits the existing budget of two endpoints with 3 clients each.
7. **Reading is open like `/api/status`; clearing is a person's command** with the password and the
   command header, refused while the roof moves.
8. **The Diagnostics page is off the main navigation**, reached from the System page, so the UI
   stays as it is for owners who don't use it.
9. **Stall current is defined only for a fault while driving** (the last 250 ms average); a move that
   arrives has no stall.
10. **The sag is measured from the last second at rest before the move**, not from a fixed 12.6 V,
    so a battery that is simply low doesn't read as sag.
11. **A move already under way when the mode is switched on isn't recorded**; recording starts with
    the next move, so no move is recorded with a wrong start.
12. **Switching "Keep move summaries" on while diagnostics are already on starts a new kept list**:
    the file is read only when diagnostics are switched on or at start-up, so ids never collide; the
    next write replaces the old file.
13. **The UI mockup review is pending**: the owner reviews the page in the demo before merging.

## Out of scope

- Any decision from diagnostics: a jam or wear alert, an automatic limit, a trend warning.
- Home Assistant discovery entities for diagnostics.
- Persisting profiles, or exporting more than one move at a time.
- A calibration run (spec 016), which may use the read interface of FR-037 later.
