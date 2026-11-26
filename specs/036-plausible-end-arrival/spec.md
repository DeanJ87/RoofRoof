# Feature Specification: Plausible Ends and a Moving Roof

**Feature Branch**: `spec/036-plausible-end-arrival`

**Created**: 2026-11-25

**Status**: Draft

## Context

During the shakedown (run B01, finding F19) a web Open drove for about 24 s. At about a third of
the travel the Hall reading rose to 2152 counts, past the open threshold of 2000 set by a re-teach
a little earlier. The controller took it as the open magnet: state `open`, 100 %. The camera showed
the roof about a third open. Full travel is about 54 s (1850 mm at about 32.6 mm/s); the close
afterwards took 23 s, which confirms where the roof was.

The consequence: Alpaca reported `ShutterOpen`, so N.I.N.A. would have started imaging under a
mostly closed roof, and the position model was wrong (the next close timed its slow-down from the
wrong end).

Two more field findings from the same night share the theme "the controller believed something the
roof wasn't doing":

- The open magnet had shifted, so there was no end reading at all. The open drove until the move's
  time guard (66.7 s against about 54 s of travel) and pushed against the mechanical end stop for
  several seconds.
- With the slow duty set to 25 %, a close from an unknown position crawled at a few millimetres a
  second for minutes. The supply barely sagged (12.42 V against 12.5 V at rest; a normal move sags
  it 0.8-1.1 V) and the Hall reading was flat (it is flat everywhere between the magnets), so
  nothing told "driving" from "moving" (survey runs `hall_close`, `hall_close80`).

The controller already estimates the position from the time and duty driven since the last magnet
(spec 002). That estimate said about 36 % when the reading appeared. Constitution I says the motion
code must stop on its target magnet; it does not say which readings count as that magnet. This
spec tightens it: an end reading must be plausible against the move's progress.

## User Scenarios & Testing

### User Story 1 - A stray reading part-way is not an arrival (P1)

The roof opens from closed. Part-way, the Hall reading crosses the open threshold for a short
stretch (a stray field, a shifted magnet, a threshold taught too close to the reading between the
magnets).

**Acceptance**:

1. The roof does not report `open`; Alpaca never reports `ShutterOpen` for it.
2. The move goes on at the slow duty toward open. When the reading drops back below the
   threshold, the roof stops with the fault "End too early: magnet or sensor problem", the
   position unknown, and a critical "Roof needs a person" alert.
3. The move summary (Diagnostics) shows the Hall peak and where it was (time and estimated
   position).

### User Story 2 - A lasting early reading is a fault, not a guess (P1)

The reading stays past the threshold far from the end (a threshold set too close to the reading
in between).

**Acceptance**:

1. The move is held at the slow duty for at most 5 s; never plausible and never clearly at the
   magnet, it stops with the same fault and alert, the position unknown.
2. After a person clears the fault, the roof still doesn't report that end and Open (or Close,
   for the closed end) toward it is refused with a reason until the roof has reached the other
   end or new Hall settings are saved (a re-teach).

### User Story 3 - A real end slightly early in the estimate still arrives (P1)

The estimate lags a little (the roof runs a little faster than its speed setting), or the reading
is clearly at the magnet.

**Acceptance**:

1. An end read at 77 % of the travel that lasts is accepted when the estimate reaches 80 %, after
   at least 1 s of the reading; the roof reports the end.
2. A reading past its threshold by at least half the gap between the two thresholds, held for 1 s,
   is accepted wherever the estimate is.

### User Story 4 - Nothing else changes (P1)

1. A normal open or close arrives as before (the estimate is past the plausible point).
2. A stop or a reversal part-way behaves as before.
3. A move from an unknown position (no estimate) stops at the first magnet as before; the log says
   the end wasn't checked.
4. Calibration legs and jogs (they measure the travel) stop at the first magnet as before.

### User Story 5 - A missing end magnet stops the move early (P1)

1. With the open magnet missing, an open from closed stops at 115 % of its expected time (at least
   3 s past it), before the hard timeout, with the fault "No end magnet where expected", the position
   unknown and the needs-person alert. By then the roof creeps at the slow duty.

### User Story 6 - Driving without moving is a fault (P1)

1. A move whose supply never sags by 0.2 V (in any reading after the first second, spec 040) and whose Hall reading doesn't change by
   60 counts for 10 s, after the first second, stops with "No movement detected" and the alert.
2. A person's jog or a calibration leg gets twice the time.
3. A healthy roof (about 1 V of sag at full duty, 0.4 V at a 40 % slow duty) never trips it.
4. The Motion settings warn when the slow duty is under 40 %.

### Edge cases

- Stop, Abort, a reversal or any other fault while an early reading is held: the move ends with
  the position unknown and that end doubted (as after the fault). A reversal starts from an unknown
  position (slow duty, first magnet).
- A fault restored after a restart doesn't record which end was read; clearing it doubts the end
  the Hall reads then.
- The doubt is not kept across a restart once the fault is cleared (see assumptions).
- The automatic close toward a doubted closed end is refused like any move toward it; the
  needs-person reminders continue while the position is unknown.

## Requirements

- **FR-001**: For a move from a known position (not a calibration leg or jog), the target's magnet
  MUST count as arrival only when the estimate has covered `roof.endPlausiblePercent` of the
  travel toward that end (default 80, range 50-95).
- **FR-002**: A target magnet read earlier ("early") MUST NOT end the move: the move MUST continue
  at the slow duty toward that end.
- **FR-003**: An early reading MUST be accepted only when it has lasted `EARLY_END_PERSIST_MS`
  (1 s) and either the estimate has reached the plausible point, or the reading has been past its
  threshold by at least half the gap between the thresholds for 1 s.
- **FR-004**: An early reading that is lost (back between the magnets), or not accepted within
  `EARLY_END_HOLD_MS` (5 s), MUST latch the motion fault `endTooEarly` (measured: the estimated
  progress in percent when it was first read; limit: the setting), stopping the motor with the
  position unknown.
- **FR-005**: `endTooEarly` MUST raise the critical needs_person alert at once, reminded while it
  stays latched, with the measured value and what a person must do (constitution III).
- **FR-006**: The state MUST stay Opening/Closing while an early reading is held, so Alpaca never
  reports `ShutterOpen`/`ShutterClosed` on it.
- **FR-007**: After the move ends while an early reading is held (fault, stop, abort, reversal),
  that end's magnet MUST NOT place the roof, and any move toward it MUST be refused
  (`endDoubted`) until the roof reaches the other end's magnet or new Hall settings are applied.
- **FR-008**: Moves from an unknown position, calibration legs and jogs MUST keep their behaviour;
  an unknown-position arrival MUST be logged as not checked.
- **FR-009**: The move summary MUST include the Hall peak (the reading furthest from the start),
  when it occurred and the estimated position then (`hallPeak`, `hallPeakMs`, `hallPeakMm`).
- **FR-010**: `roof.endPlausiblePercent` MUST go through the config model, its validation, the web
  schema and Settings → Roof → Motion; old configs load it as 80.
- **FR-011**: The new fault kind MUST be appended (stored kinds keep their numbers).
- **FR-012**: The firmware MUST stay within the flash budget (about 90 % of the OTA slot).
- **FR-013**: A move from a known position (not a calibration leg or jog) that has driven
  `roof.endOverrunPercent` (default 115, range 105-200) of its expected time, and at least
  `END_OVERRUN_MIN_MS` (3 s) past it, without its target magnet MUST latch `noEndMagnet`
  (measured: ms driven; limit: the overrun limit), unless an early reading is being held.
- **FR-014**: Any move or calibration leg MUST latch `noMovement` (measured: the deepest sag in the
  window; limit: the setting) when, after the first `BLIND_SURGE_MS` (1 s) of driving, neither a
  supply sag of `roof.motionSagVolts` (default 0.2 V, 0 = off, else 0.05-2) in any reading (spec 040; was held `CONFIRM_MS`) nor a
  Hall change of `MOTION_HALL_COUNTS` (60) has been seen for `roof.motionCheckSeconds` (default 10,
  range 5-60); twice that for a calibration leg or a person's jog.
- **FR-015**: `noEndMagnet` and `noMovement` MUST raise needs-person at once like `endTooEarly`.
- **FR-016**: Settings → Roof → Motion MUST warn (a note) while the slow duty is under 40 %.

## Success Criteria

- **SC-001**: The field case (a reading past the open threshold at about a third of the travel)
  replayed in a native test ends in `endTooEarly` with the roof below 40 %, never `open`.
- **SC-002**: The existing roof, calibration, close-sequence and diagnostics tests pass unchanged
  except one that read the closed magnet 3 s into a 50 s close.
- **SC-003**: Flash grows by under 3 KB.
- **SC-004**: The shifted-magnet case stops before the hard timeout at the slow duty; the 25 % crawl
  case stops 11 s after the drive starts; a full open and close with 1 V of sag at full duty pass.

## Assumptions decided for the owner

- **Plausible point: 80 % of the travel**, by the estimate, measured over the full travel (not the
  move's own distance): a move from part-way is judged where the roof is, and the estimate's error
  grows with the travel driven since the last magnet. Setting `roof.endPlausiblePercent`, 50-95.
- **The hold: 5 s at the slow duty**, then the fault. At the default slow speed that is about 4 % of
  the travel, so a roof up to about a third faster than its speed setting still arrives; faster,
  it stops with the fault (the speed setting is wrong: calibrate). Fixed, not a setting.
- **The reading must last 1 s** (on top of the Hall filter's 200 ms) before an early reading is
  accepted, and a lost reading faults at once: a magnet the roof drives past is not an end.
- **Clearly at the magnet** means past the threshold by half the gap between the two thresholds
  (default 3000/1100: 3950 or 150, effectively out of reach; field thresholds near 2000/1470: about
  2265, above the stray 2152). It never makes a weak stray an arrival; it lets a strong real magnet
  win over a stale speed setting.
- **A stop or reversal while held doubts the end** like the fault: the roof is not where the magnet
  says, so the safe reading is "unknown".
- **Doubt clears** only on the other end's magnet or new Hall settings (a re-teach or a calibration
  run's save), not on an unrelated settings save. It is not kept across a restart once the fault is
  cleared: the fault itself is kept, and a person has looked at the roof to clear it. Bench check.
- **Moves from an unknown position** keep stopping at the first magnet (constitution I's rule for
  them); they have no estimate to judge by. The log says so, so a pattern shows up.
- **The overrun: 115 % of the expected time, at least 3 s more**, a setting
  (`roof.endOverrunPercent`, 105-200). Time-based, as the field asked. Because the profile creeps
  at the slow duty once the estimate has arrived, a roof more than about 7 % slower than its speed
  setting reaches 115 % before its magnet: it then faults until calibrated or the setting is
  raised. A wrong speed setting is better found than hidden. The logic also accepts 0 (off) for
  tests that isolate another limit; the stored setting can't be 0.
- **The movement check: 0.2 V and 10 s by default**, not the 0.3 V first suggested. The field
  roof sags about 1 V at full duty and 0.09 V at 25 %; at the default 40 % slow duty a linear
  share is about 0.4 V, but the 25 % reading shows the sag falls faster than the duty, so 0.3 V
  risked stopping a healthy automatic close at the slow duty. 0.2 V still catches the 25 % crawl.
  Measuring the sag at the slow duty on the roof is a bench check. 0 turns it off (a lab supply or
  a large battery may not sag). The window restarts on any sign of movement; the first second (the
  start-up surge) isn't judged. A calibration leg or a jog gets twice the window.
- **The movement check applies to the automatic close too.** A false trip leaves the roof part-open
  with a fault and a needs-person alert; a crawl that never arrives does the same, only minutes
  later and with the motor driven the whole time. The blind close keeps its own stall and time
  limits and isn't judged.
- **The slow duty is warned, not forced.** A minimum of 40 % in validation would make stored
  settings at 25 % invalid after an upgrade and refuse every move; a note under the Motion fields
  says why 40 % or more, and the movement check catches the crawl.
- **The logic's defaults** keep the movement check off (`Settings{}`), while the stored setting's
  default is 0.2 V: the native tests' simulated roof has no supply sag unless a test sets it. The
  demo and bench roof sag 1.0 V at full duty (as the field roof), so they run with the check on.
- **No current-sense or position input** is added: the estimate is the only plausibility source.
- **The Hall peak is not kept** in the stored summaries (their record format is unchanged); it is
  in the held summaries and the live move.
- **No UI beyond** one number field beside the leave time (the existing field pattern), a
  Diagnostics row and a fault label; the needs-person alert carries the explanation.
