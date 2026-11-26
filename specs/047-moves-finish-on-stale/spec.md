# Feature Specification: A Move Finishes When N.I.N.A. Stops Answering

**Feature Branch**: `spec/047-moves-finish-on-stale` | **Created**: 2026-11-26 | **Constitution**: 11.1.0 -> 12.0.0 (II)

## Context

**Findings F36/F36b (the real roof, three times in one day).** An automatic close (and once an
open) started with the park confirmed. Then the network dropped (the access point down, the roof
controller off WiFi), N.I.N.A.'s park reading went past its 10 s freshness, and the park interlock
(`RoofIo::interlockLost` / `closeMayContinue`, spec 021's ride-through) stopped the motor and
latched "Park lost during the close: N.I.N.A. not read within its freshness limit". The roof was
left part-open. Under spec 043's rule (the telescope may move only with the roof at an end) a
part-open roof is the worst place to stop: neither the roof nor the telescope may move until a
person comes.

**Owner decision (2026-11-26):** a command is a command. A move that started on a confirmed park
should not stick because the WiFi dropped.

**Findings F45/F46 (the weather source).** The safety poller waited 3 s for SQMeter's reply; a
busy SQMeter answering in 5 s and more counted as unreachable and could close the roof. The
weather was polled every 5 s whatever the roof did, so rain during an open was seen up to 5 s late.
N.I.N.A. and SQMeter share the poller task: a slow weather reply could hold a N.I.N.A. read past its
freshness.

**Finding (fault text).** An open stopped by the interlock said "Park lost during the close".

## User Scenarios & Testing

### User Story 1 - The close finishes on a dropped network (P1)

Rain; N.I.N.A. parks the mount; the park is confirmed; the automatic close starts. Ten seconds in,
the access point restarts. N.I.N.A. can't be read for two minutes.

1. The close goes on to the closed magnet. The log says "Close continues without a fresh park
   reading (N.I.N.A. unreachable): it started with the park confirmed".
2. At the closed magnet the log and an information alert say "Finished the close without a fresh
   park reading (N.I.N.A. unreachable)".
3. The next Open needs a fresh confirmed park, as before.

The same for an open (a person's, Alpaca's, MQTT's), and with the poller not reading at all (the
reading ages past its freshness).

### User Story 2 - Real evidence still stops it (P1)

During that close N.I.N.A. comes back and answers that the mount is not at park (or slewing, or
not connected): the motor stops at once with "Park lost during the close: park not confirmed".
During an open: "Park lost during the open". A park switch opening, any motion fault, Stop, and
for an open unsafe weather and daylight, stop it as before.

### User Story 3 - The weather is read patiently, and faster while moving (P2)

SQMeter answers in 6 s: the reply is taken. While the roof opens, SQMeter is read every 1.5 s. A
SQMeter that takes the full 8 s on every poll never leaves N.I.N.A.'s reading older than 10 s.

### Edge cases

- N.I.N.A.'s last answer said "not parked" and then it went silent: the move already stopped on
  that answer; silence never revives it.
- N.I.N.A.'s address removed or changed during a move ("not configured", "not read yet"): stops,
  as before (a new address's silence proves nothing).
- The main loop stalls (the hand-over of the safety inputs is 5 s old): stops, as before.
- The park switches only: nothing from N.I.N.A. is relied on; unchanged.
- A person's confirmed close or open, the N.I.N.A.-lost close, a person's jog toward closed: not
  watched by the interlock, as before; nothing is reported for them.
- A move that goes on stale and then stops short (Stop, a fault, a switch): no "finished" report;
  its own stop says what happened.

## Requirements

- **FR-001**: `Safety::CloseHold::unanswered`: not parked only for want of a fresh N.I.N.A. answer
  (unreachable, unreadable, API error, stale), with N.I.N.A.'s last answer (the stale reading, or
  the one remembered across failed reads) judged as it was, with the switches as read now, saying
  parked. Not for "not configured" or "not read yet", nor with the switches alone.
- **FR-002**: `RoofIo::closeMayContinue` MUST hold, with every fitted switch closed, while the park
  result says parked, the ride-through is fresh, or `parkUnanswered` holds, however old the
  reading. The start check (`gateCommand`, `parkHolds`) and spec 043's park requests are unchanged.
- **FR-003**: `currentGates` (a stalled hand-over) clears `parkUnanswered` with the rest.
- **FR-004**: `RoofIo::movesOnStale` and `StaleRide`: the control step logs when a watched move
  begins going on without a fresh park reading, and logs and raises the information alert
  `finished_without_park` (Normal, no setting) when it reaches the end it was going to.
- **FR-005**: The interlock's fault text names the direction: `InterlockDuringOpen` in the record
  (`interlockRecordBits`), "Park lost during the open"; records stored before keep "close".
- **FR-006**: SQMeter's reply may take 8 s in all (`Safety::SQMETER_LIMITS`: 1.5 s to connect, 6.5 s
  for the reply, the body until 8 s); N.I.N.A.'s are unchanged (3 s, 3 s, 6 s).
- **FR-007**: The weather is polled every 1.5 s while the roof moves (`RoofControl::moving()`), 5 s at
  rest (`Safety::PollSchedule`).
- **FR-008**: N.I.N.A. is read before each weather poll unless its last read ended within 1 s, so the
  park reading stays within `NINA_FRESH_MS` through the longest weather poll
  (`NINA_BEFORE_WEATHER_MS + SQMETER_TOTAL_MS < NINA_FRESH_MS`, a static assert). Exact for a plain
  http SQMeter; an `https://` one can add the wait for the TLS session (1 s) and its handshake (up
  to 3 s), so in the worst case the reading can pass 10 s for a few seconds. A move under way no
  longer stops on that (FR-002); only a new move's start waits for the next reading.
- **FR-009**: No new task and no new buffer kept for the device's life (heap budget unchanged); the
  poller's stack unchanged.
- **FR-010**: Docs: close-on-unsafe (a new section, the alert), safety-inputs (the interlock, the
  timeouts, the poll), alerts, REST; changelog; constitution 12.0.0.

## Constitution rules relied on

- II: close (and open) only onto a confirmed park: the start check is unchanged. **Amended
  (12.0.0)**: a move that started on a confirmed park goes on when N.I.N.A. merely stops answering;
  real evidence of "not parked" still stops it. The telescope moves only with the roof at an end
  (11.1.0): this is why stopping part-way is worse than finishing.
- III: an open stops for unsafe weather and daylight (unchanged); rain seen sooner while moving.
- V: the rules are pure (`ParkHold`, `RoofGates`, `StaleRide`, `PollSchedule`) and tested natively.
- VI: outgoing HTTP stays on the one poller task; no new task or lasting buffer.

## Why not a second poller task

Splitting N.I.N.A. onto its own task would make the park read independent of SQMeter outright,
but it costs a second 8 KB stack (N.I.N.A. may be `https://`, so it needs room for a TLS
handshake) from a heap that specs 038 and 045 fought for. Bounding SQMeter's whole request (8 s)
and reading N.I.N.A. just before each weather poll keeps the reading fresh by construction (a
static assert and a simulated worst case) at no memory cost.

## Tests

- `test_moves_finish_on_stale` (the control step with the simulated roof and the park pipeline): a
  close finishes when N.I.N.A. becomes unreachable, an open when the poller goes silent (with the
  log and alert texts); no report with fresh readings; a fresh not-at-park, slewing or
  not-connected answer stops either move with the direction's text; a switch opening; Stop and a
  fault; an open's weather and daylight stops; no start on a stale or unanswered park;
  `unanswered`'s conditions; overridden and N.I.N.A.-lost moves not reported; the tracker; the
  alert's type, level and name.
- `test_poll_schedule`: the 8 s weather limits; 1.5 s while moving, 5 s at rest; N.I.N.A. every
  2 s and before a weather poll; a slow (and a varying) SQMeter never leaves the park reading past
  10 s, moving or not (fails without FR-008).
- Updated: `test_park_grace` (the freshness bound no longer stops a move), `test_roof_status` (the
  open's fault text), `test_roof_alert_settings` (the new alert has no setting).

## Owner checks (pending)

- [ ] Bench (bench mode, no motor): start a close with the park confirmed, stop N.I.N.A. (or pull
  the access point): the close finishes, the log line and the information alert arrive; start the
  next move: refused for the park until N.I.N.A. is back.
- [ ] Bench: during a close, unpark in N.I.N.A.: the close stops with "Park lost during the close";
  during an open: "Park lost during the open".
- [ ] A slow SQMeter (5-7 s replies) no longer reads unreachable; the log's N.I.N.A. reading stays
  under 10 s old while SQMeter is slow; SQMeter's request log shows about 1.5 s between polls while
  the roof moves.
