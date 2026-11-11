# Feature Specification: Calibration Run

**Feature Branch**: `spec/016-calibration-run`

**Created**: 2026-11-11

**Status**: Draft

## Context

The roof's real numbers are settings: the travel and the speed (from which every move's timeout
and slow-down come), the timeout margin, which end's magnet reads high and the two Hall
thresholds. Today the owner finds them by hand: the Hall thresholds from the live reading
(commissioning stage 3, the wizard's Hall page, Settings → Roof → Hall sensor), and the speed
and margin from a stopwatch over several attended runs at 40 %, 70 % and 100 % duty
(commissioning stage 8). It is slow, easy to get wrong, and a wrong polarity or threshold only
shows up as a fault on the real roof.

This spec adds an attended **calibration run**. A person at the roof starts it from its own page
of the web UI. The controller drives the roof to the closed end (if it isn't there), then opens
and closes it at the slow duty, and, if asked, opens and closes it again at the top duty. Every
leg is bounded by a time limit, the existing jam, over-current and supply checks and the drive
budget, and stops at the first magnet it reaches. While it drives the controller measures each
leg's time at a known duty, the Hall reading at rest at each end, the in-between reading and its
noise, and the current and supply. At the end it **proposes** a speed (from the travel the owner
measured), a timeout margin, which end reads high and the two thresholds, each beside the value
it would replace. Nothing changes until the owner accepts, and then only through the settings
save path every other setting uses.

The run never decides anything the motion logic doesn't already decide: it asks the same
controller for each leg, the same gates check it, and the same faults stop it.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Who can start a run? → A: Only a person, from the web UI (authenticated like every roof
  command, with the `X-RoofRoof` header), with an explicit confirmation that they are at the roof
  and can see it. Alpaca, MQTT, Home Assistant, the close sequence and the daylight close have no
  way to start one, and the device never starts one by itself.
- Q: Which legs, in which order? → A: From where the roof is, a slow close to the closed end
  (skipped when the person says it is already closed); then a slow open and a slow close, each a
  full measured travel; then, only if the person asks and the top duty is above the slow duty, an
  open and a close at the top duty. The run always ends at the closed end.
- Q: Unsafe weather: refused, or allowed because the first leg is a close? → A: Refused. The run
  must open the roof, which constitution III forbids while the weather isn't safe, so a run
  started in bad weather could only close the roof and stop, which the close sequence already
  does better. Each open leg is checked again when it starts. If the weather stops being safe
  (or daylight wants the roof closed) during a run, an opening leg stops at once (the existing
  open gate), a closing leg finishes its close, and the run ends with no proposal.
- Q: Does the run need the mount parked? → A: Yes. Every close does (constitution II), and the
  run closes the roof at least once, so the park must be confirmed when the run starts and again
  when each close leg starts, and the existing park interlock stops a close leg as a motion fault
  if the park is lost. If the park stops holding at any time during the run, the run ends: an
  opening leg is stopped, and the roof is left for the person to close.
- Q: How does the run know which end the roof is at, when the polarity may be wrong? → A: The
  person says where the roof is now (closed, open or part-way) when they start it; the device
  checks that this agrees with the Hall reading (a magnet at an end, none part-way) and refuses
  otherwise. From then on the run tells the ends apart by the direction it drove, never by the
  polarity setting, so it can measure the polarity.
- Q: What stops a leg? → A: The first confirmed magnet after it has left the one it started on
  (constitution I: a move from an unknown position stops at the first magnet), using the current
  thresholds. A leg whose end is already known (the slow close and the top legs) must reach that
  end's level; the other level is the existing wrong-magnet fault. A leg is a fault, latched until
  a person clears it, if it times out, doesn't leave its start magnet in the leave time, exceeds
  the drive budget, draws over-current, loses its current reading or its supply, or the Hall
  reading reaches a rail: exactly the existing faults.
- Q: How long may a leg drive? → A: A new setting, `roof.calibrationLimitSeconds` (default 180 s,
  30-600 s), bounds the first two legs (the time a slow travel may take). The slow close is
  bounded by 1.5 times the slow open's time plus 5 s, and each top-duty leg by 1.25 times the
  slow leg in its direction plus 5 s, never more than the setting. During a leg the drive budget
  is the leg's limit (the old settings' budget may be wrong, which is what the run is for);
  stopped and restarted legs still add up toward it.
- Q: Can the travel be measured? → A: No: the Hall sensor only sees the two ends. The person
  enters the travel they measured with a tape between the two magnets' trigger points (the
  current setting by default); the run proposes the speed that makes the measured times match it.
- Q: How are the thresholds proposed, when the roof stops where the current thresholds say? → A:
  The run keeps each threshold where it is when it is clear of the in-between noise and read at
  rest at its end, and moves it only as far as needed: toward the in-between level so the end
  reads clearly past it at rest, or away from it to clear the noise, never past the reading at
  rest. If no threshold satisfies both, it proposes none and says why. It never moves an end's
  stopping point further toward the end stop than where the roof stopped during the run.
- Q: When does it refuse to propose thresholds? → A: When both ends read on the same side of the
  in-between level (the magnets face the sensor with the same pole); when an end reads less than
  300 counts from the in-between level (the magnet is too weak or too far from the sensor); or when
  no threshold is both clear of the noise and read at rest. The other proposals (speed, margin)
  stand on their own.
- Q: What happens to other commands during a run? → A: Stop from anyone, and the run's own Stop,
  end the run (a person's Stop counts as a person's, as today). Open and close from a person, an
  imaging app or MQTT are refused with "calibrating" until the run ends. The automatic close (bad
  weather or daylight) always wins: it ends the run, a closing leg carries on as its close, and
  anything else goes through the automatic close's normal checks.
- Q: Settings saves, uploads and restarts during a run? → A: Held for the whole run, as while the
  roof moves, including the pauses at each end: the run's thresholds and timings must not change
  under it.
- Q: What happens after a restart or a power cut mid-run? → A: The run is gone (it is kept in
  memory only) and nothing moves. The roof is wherever it stopped and the usual start-up rule
  applies: unknown between the magnets, the end the saved settings name at a magnet. The settings
  are the ones from before the run.
- Q: Is the current-sense calibration proposed? → A: No. With current sense fitted the run records
  the running and peak current and the lowest supply and shows them beside the limits, with a
  warning when the peak reaches 80 % of the over-current limit; the limits stay the owner's
  decision (commissioning stage 8).
- Q: How is a proposal accepted? → A: The run's page shows each proposed value beside the saved
  one; "Accept and save" sends the proposed values through the same settings save as Settings (the
  web UI's validation, then the device's, refused while the roof moves), and "Discard" changes
  nothing. The device never applies a proposal by itself.
- Q: Where does it live in the UI? → A: Its own page (`/calibrate`) with the start form, the live
  run with a Stop button that is always shown while the run is active, and the result. Settings →
  Roof gets a Calibrate card (the time limit setting and a link to the page), and the setup wizard
  gets a page after the Hall page that explains the run and links to the page; the wizard itself
  still never moves the roof.

## Constitution rules this spec relies on

| Principle | Rule | Where it is kept |
|---|---|---|
| I | Only a person may move a roof whose position is unknown, at reduced speed, to the first magnet | FR-001, FR-006: started only by an authenticated person who confirms they are at the roof; every leg from an unknown position runs at the slow duty and stops at the first magnet |
| I | Every move has a hard timeout enforced by the motion code and stops on its magnet | FR-007, FR-008: each leg's limit is the controller's own timeout; each leg stops at its end's magnet |
| I | Any motion fault stops the motor, drops EN and locks out until a person clears it; a restart never clears it | FR-009: a leg's faults are the existing ones, through the existing latch and fault store |
| I | Settings that change motion, OTA and restarts are refused while moving | FR-016: held for the whole run |
| I | Never move by itself with the position unknown; unknown after a restart part-way | FR-018: nothing resumes after a restart |
| II | No close unless the mount is confirmed parked; the interlock is re-checked throughout | FR-004, FR-011: the park is confirmed at the start and before each close leg; the existing interlock watches each close leg |
| III | No opening while the weather isn't safe; unsafe ends closed | FR-004, FR-012: refused while not safe, open legs re-checked, an opening leg stopped when it turns, a closing leg finishes, the automatic close wins |
| III | Refusals state the value and the limit or the input that failed | FR-005: every refusal and failure carries the existing texts |
| V | Every safety rule has a native test | FR-019 |
| VII | Stop reachable and never disabled while the roof moves | FR-021: the run's page shows Stop for the whole run; the Stop bar shows while a run is active |
| VIII | Endpoints that move the roof call `requireAuth` | FR-020 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Measure the roof instead of timing it by hand (Priority: P1)

The owner has wired the roof, set rough Hall thresholds from the live reading and checked the
motor direction with a first short move. Standing at the roof with the mount parked, SQMeter safe
and the roof closed, they open the calibration page, confirm they are at the roof, say the roof
is closed, check the travel they measured with a tape and start. The roof opens and closes at the
slow duty. The page shows each leg as it runs, with Stop always in reach. At the end it shows the
proposed speed, timeout margin, polarity and thresholds beside the saved values, with the
measured times, the Hall readings and the current. The owner accepts and the settings are saved.

**Why this priority**: This is the feature.

**Independent Test**: In the demo, start a run from the closed end; when it ends, the proposal
shows the values that match the simulated roof, beside the saved ones; Accept saves them; nothing
was saved before.

**Acceptance Scenarios**:

1. **Given** the roof closed, the weather safe, the mount parked and no fault, **When** the person
   confirms they are at the roof and starts a run, **Then** the roof opens and closes at the slow
   duty, each leg stopping at its end's magnet, and the run ends at the closed end with a proposal.
2. **Given** a finished run, **When** the page shows the result, **Then** each proposed value is
   shown beside the saved one, with the measured leg times, the in-between Hall level and noise,
   each end's reading and (with current sense fitted) the running and peak current.
3. **Given** a proposal, **When** the person chooses Accept and save, **Then** the values are saved
   through the settings save and the controller uses them; **When** they choose Discard, **Then**
   nothing changes.
4. **Given** the roof part-way, **When** the person says it is part-way and starts a run, **Then**
   the first leg closes slowly to the first magnet and the run carries on from the closed end.
5. **Given** the person asks for the top-duty pass, **When** the slow legs have finished, **Then**
   the roof opens and closes again at the top duty, slowing to the slow duty before each end, and
   the speed is proposed from those legs.

---

### User Story 2 - Stop always works and changes nothing (Priority: P1)

Part-way through a run the owner sees something wrong and presses Stop on the page (or the Stop
bar, or the dashboard). The roof stops at once, the run ends as stopped, no proposal is made and
the settings are as they were.

**Why this priority**: Constitution I and VII.

**Independent Test**: Start a run in the demo, press Stop mid-leg: the roof stops on that step,
the run reports "stopped", the saved settings are unchanged.

**Acceptance Scenarios**:

1. **Given** a leg driving, **When** a person presses Stop (the run's own or any other), **Then**
   the motor stops on that step and the run ends with no proposal.
2. **Given** the run pausing at an end between legs, **When** Stop is pressed, **Then** the run
   ends and no further leg starts.
3. **Given** a run ended by Stop, **Then** every setting is the value it had before the run.
4. **Given** a run in progress, **When** the device restarts, **Then** after the restart no run is
   active and nothing moves.

---

### User Story 3 - The run refuses when it isn't safe to run (Priority: P1)

The owner tries to start a run while it is raining, with the mount unparked, with a fault latched,
while the close sequence is busy, or in daylight with the daylight close on. Each time the page
says why, with the value or the input that failed, and nothing moves.

**Why this priority**: Constitution II and III.

**Independent Test**: For each refusal, the start request is answered 409 with the reason and its
text, and the roof doesn't move.

**Acceptance Scenarios**:

1. **Given** the weather not safe (or no verdict yet), **When** a run is started, **Then** it is
   refused with the verdict's reason.
2. **Given** the mount not confirmed parked, **When** a run is started, **Then** it is refused with
   the park's reason.
3. **Given** a fault latched, the close sequence not idle, daylight wanting the roof closed, the
   roof moving or a close waiting, an upload, a restart, the setup hotspot, a low supply, an
   unsettled Hall reading or invalid settings, **When** a run is started, **Then** it is refused
   with that reason.
4. **Given** the person didn't confirm they are at the roof, **When** a run is started, **Then** it
   is refused.
5. **Given** the person says the roof is closed but no magnet is read (or part-way but a magnet is
   read), **When** a run is started, **Then** it is refused, saying the Hall reading disagrees.

---

### User Story 4 - A leg that goes wrong is a fault (Priority: P1)

The open magnet has come loose. The slow open leg drives past where the magnet should be and onto
the rubber stop. The leg stops at its time limit with a `timeout` fault, latched as any other; the
run ends as failed, says which leg and why, and proposes nothing. The owner fixes the magnet,
clears the fault and runs again.

**Why this priority**: Unknown travel is the hazard this spec adds; every leg must be bounded.

**Independent Test**: In the native tests, a plant with a missing magnet, a jam, a backwards
motor, a sagging supply, an over-current and a Hall rail each give the existing fault on the leg,
within its limit, and a failed run with no proposal.

**Acceptance Scenarios**:

1. **Given** a leg that reaches no magnet, **When** its limit passes, **Then** the motor stops with
   a `timeout` fault (the limit and the time driven), the fault is latched and the run fails.
2. **Given** a leg that doesn't leave its start magnet within the leave time, **Then** it faults
   `didNotLeave`.
3. **Given** current sense fitted and the roof jammed, **Then** the leg faults `overCurrent` within
   the existing confirmation time.
4. **Given** a slow close that reads the open end's level, **Then** it faults `wrongMagnet`.
5. **Given** a leg stopped and restarted (a new run) without reaching a magnet, **Then** the drive
   budget still bounds the total time driven toward that end.

---

### User Story 5 - Polarity and threshold problems are found, not saved (Priority: P2)

The owner's sensor reads high at the closed end, but the setting says open. The run finds the
closed end reads high and proposes "closed" beside "open". On another roof both magnets were
fitted the same way round: the run finishes, proposes the speed and margin, and refuses to propose
thresholds, explaining that both ends read high so the magnets face the sensor with the same pole.

**Why this priority**: The polarity is the setting most often wrong on a first run.

**Independent Test**: Native tests with a plant whose high end is the other one, whose ends read
the same level, and whose magnet is weak.

**Acceptance Scenarios**:

1. **Given** the high end opposite to the setting, **When** the run finishes, **Then** it proposes
   the measured high end.
2. **Given** both ends on the same side of the in-between level, **Then** no thresholds or
   polarity are proposed, with the "same pole" explanation, and the run still returns the roof to
   the closed end.
3. **Given** an end less than 300 counts from the in-between level, **Then** no thresholds are
   proposed, with the "too weak or too far" explanation and the two readings.
4. **Given** thresholds that are already clear of the noise and read at rest, **Then** they are
   proposed unchanged.

---

### User Story 6 - Calibrate from the setup wizard and from Settings (Priority: P2)

After the Hall page, the setup wizard shows a Calibration run page explaining what the run does
and what it needs (at the roof, the motor connected, the mount parked, the weather safe), with a
button that opens the run's page; the wizard page itself has no motion control. Settings → Roof
has a Calibrate card with the time limit setting and the same link.

**Independent Test**: The wizard's calibration page renders no Open, Close or Start control and
links to `/calibrate`; Settings → Roof shows the card with the time limit field.

**Acceptance Scenarios**:

1. **Given** the wizard at the Hall page, **When** the owner continues, **Then** the next page is
   the Calibration run page with a link to the run's page and "Save and continue".
2. **Given** Settings → Roof, **Then** the Calibrate card shows `roof.calibrationLimitSeconds` with
   its range and a link to the run's page.

---

### User Story 7 - Try it in the demo (Priority: P3)

A visitor to the live demo opens the calibration page, starts a run, watches the simulated roof
open and close, and sees a proposal they can accept or discard.

**Independent Test**: The demo's handlers answer the three routes from the demo core; a
Playwright walk-through starts a run, waits for the result and accepts it.

### Edge Cases

- The person says closed and the roof really is open (polarity unknown, so the magnet can't tell):
  the slow open pushes into the open end stop and faults `didNotLeave` within the leave time. The
  page asks the person to look before saying where the roof is.
- The motor direction setting is wrong: from an end, the first leg pushes into the end stop and
  faults `didNotLeave`. From part-way, the first close reaches the open end's magnet and the slow
  open then drives to the closed end, which looks consistent to the run; so after the slow open the
  run asks the person whether the roof is now fully open, and "No" ends it (`wrongEnd`) with
  nothing proposed. The same question catches a wrong answer about where the roof was combined with
  a backwards motor.
- The current thresholds can't see a magnet: the leg times out. The page says the run needs
  thresholds that already see both magnets (set them from the live reading first).
- Unsafe weather arrives during a closing leg: the leg finishes its close; the run ends with no
  proposal; the close sequence then finds the roof closed.
- The automatic close starts during a pause or an opening leg: the run ends; the automatic close
  runs with its normal checks.
- An MQTT connect or a Wi-Fi change is under way when a leg is due: the leg waits up to 15 s, then
  the run fails as busy (the roof is at an end, not moving).
- The open and close legs disagree by more than 30 %: the speed and margin aren't proposed
  (something slipped or a magnet was read early); the thresholds still can be.
- The top duty is the slow duty: the top-duty pass isn't offered.
- A fault is latched by anything during a run (the watchdog, the interlock): the run fails with
  that fault.
- The roof's settings change during a run: they can't, saves are held; a save already waiting at
  the start refuses the start.
- Two browsers: the run is one per device; a second start is refused with "calibrating"; both see
  the same status.

## Requirements *(mandatory)*

### Functional Requirements

**Starting**

- **FR-001**: A run MUST start only from `POST /api/roof/calibration/start` from an authenticated
  person (`requireAuth`, the `X-RoofRoof: 1` header), with `attended: true` in the request. No
  other path (Alpaca, MQTT, Home Assistant, the close sequence, the daylight close, a schedule,
  the setup wizard's own pages) may start one.
- **FR-002**: The request MUST say where the roof is (`closed`, `open` or `partWay`), the travel in
  mm (100-20000), and whether to run the top-duty pass. It MUST be refused when `closed` or `open`
  is given and no magnet is confirmed, when `partWay` is given and a magnet is confirmed, and when
  the top-duty pass is asked for with the top duty at or below the slow duty.
- **FR-003**: A run MUST be refused while another run is active, the close sequence is in any
  phase but idle, a fault is latched, the roof is moving, a close waits, settings are waiting to
  be applied, or the controller would refuse a move (supply low or unreadable, current unreadable,
  Hall not settled or at a rail, settings invalid).
- **FR-004**: A run MUST be refused while the weather verdict isn't safe, while daylight wants the
  roof closed, while the mount isn't confirmed parked (the close gate, constitution II), and while
  anything blocks an open (an upload, a settings save, an MQTT connect, a Wi-Fi change, a scheduled
  or committed restart, the setup hotspot).
- **FR-005**: Every refusal MUST carry a machine reason and the text the existing checks use, with
  the measured value and the limit or the input that failed.

**The legs**

- **FR-006**: The run MUST drive, in order: a slow close to the closed end unless the roof is
  closed; a slow open; a slow close; and with the top-duty pass, an open and a close at the top
  duty. Slow legs drive at the slow duty throughout; top-duty legs ramp up from the slow duty over
  the ramp-up time and drop back to the slow duty before the end: they start slowing down at half
  the time the slow leg predicts at the top duty (allowing the motor a deadband of up to half the
  slow duty), less the ramp-down time. A leg above the slow duty MUST start on
  a confirmed magnet.
- **FR-007**: Each leg MUST be a move of the existing controller, stepped by the control task, with
  its own hard timeout: `roof.calibrationLimitSeconds` for the first close and the slow open; the
  smaller of that and 1.5 × the slow open's time + 5 s for the slow close; the smaller of that and
  1.25 × the slow leg in the same direction + 5 s for each top-duty leg.
- **FR-008**: Each leg MUST stop at the first confirmed magnet after it has left its start magnet.
  Its end is told by the direction driven, not by the polarity setting. A leg whose end level is
  already known (every leg after the slow open) MUST fault `wrongMagnet` on the other level.
- **FR-009**: Each leg MUST be subject to the existing motion faults: timeout (its limit),
  didNotLeave (the leave time), drive budget (the leg's limit, counting toward the same per-end
  total), over-current and current unreadable (with current sense), supply low, Hall rail, a late
  control step, the watchdog and the park interlock. A fault MUST latch as today (needing a
  person to clear it, kept across a restart) and end the run as failed.
- **FR-010a**: After the slow open the run MUST wait (at most 5 minutes) for the person to confirm
  through `POST /api/roof/calibration/confirm` (authenticated, with the command header) that the
  roof is now fully open, before the slow close. "No" ends the run as failed (`wrongEnd`), no
  answer ends it (`unconfirmed`); either way nothing is proposed. The weather and park rules apply
  while it waits.
- **FR-010**: Between legs the run MUST wait at the end for 1 s (longer than the dead time), taking
  the Hall reading at rest, before the next leg starts. A leg that can't start because something
  blocks it MUST wait up to 15 s, then the run fails as busy.
- **FR-011**: Every close leg MUST pass the close gate (the park confirmed) when it starts. The
  park interlock MUST watch every calibration leg, not only closing ones (until the open end is
  confirmed an "opening" leg may be closing the roof): the park lost while a leg drives is a motion
  fault. If the park stops holding during a pause, the run MUST end.
- **FR-011a**: The top-duty pass MUST be skipped when the slow open and slow close disagree by more
  than 30 %; a leg above the slow duty MUST have a point to slow down at.
- **FR-012**: Every open leg MUST pass the open gate (weather safe, no daylight close) when it
  starts. If the weather stops being safe or daylight wants the roof closed during a run, an
  opening leg MUST stop at once, a closing leg MUST finish, and the run MUST end with no proposal
  and no further leg.

**Other commands during a run**

- **FR-013**: Any accepted Stop or Abort (from anyone) and `POST /api/roof/calibration/abort`
  MUST stop the roof at once and end the run with no proposal. The run's abort counts as a
  person's stop.
- **FR-014**: Open and close from a person, an imaging app or MQTT MUST be refused with reason
  `calibrating` while a run is active.
- **FR-015**: The automatic close MUST end the run and then run with its normal checks; a closing
  leg under way carries on as its close.
- **FR-016**: While a run is active (pauses included), the device MUST hold what it holds while the
  roof moves: settings saves, restores, uploads, restarts, Wi-Fi changes; `moving` reports true.

**Results**

- **FR-017**: A run that ends normally MUST report each leg (its time driven, its duty, the level
  it ended on, the speed it implies at 100 % duty and, with current sense, the mean and peak
  running current and the lowest supply), the Hall reading at rest at each end (mean, min, max),
  the in-between reading (mean, min, max and noise, from the middle half of each measured leg)
  and a proposal:
  - `travelMm`: the travel the person entered;
  - `speedMmPerSecond`: travel ÷ the duty-weighted time of the top-duty legs (the slow legs
    without the top-duty pass), rounded to 0.1 mm/s; withheld when the open and close legs differ
    by more than 30 % or a leg took under 3 s;
  - `timeoutMarginSeconds`: the larger of 5 s, 20 % of a full move at the proposed speed, and
    1.5 × the most a measured leg took longer than the proposed speed predicts + 3 s, rounded up to
    a second, within its range; withheld with the speed;
  - `hallHighEnd`: the end whose reading at rest is above the in-between level;
  - `hallHighCounts` and `hallLowCounts`: each kept where it is if it is at least the clearance
    (the larger of 100 counts and 4 × the noise) beyond the in-between band and read at rest at its
    end (the end's lowest reading at rest past it by the larger of 10 counts and 3 × the rest
    noise); otherwise moved only as far as needed; withheld with the polarity (and the reason)
    when the ends read on the same side, an end is less than 300 counts from the in-between level,
    or no threshold meets both;
  - the saved value of each, from the start of the run.
- **FR-018**: A run that is stopped, fails or is ended by the weather, the park or the automatic
  close MUST make no proposal and MUST leave every setting unchanged. The run MUST be kept in
  memory only: after a restart no run is active, nothing resumes and the roof follows the usual
  start-up rule.
- **FR-019**: The run's decisions (the legs, their limits, the waits, the endings, the statistics,
  the proposal and every refusal) MUST be pure logic in `lib/` with native tests covering each
  rule and boundary, including a full simulated run for each plant fault.
- **FR-020**: `GET /api/roof/calibration` MUST return the run's state (idle, running, done, failed,
  stopped), the current leg and its limit, the reason and text of an ending, the legs, the
  readings and the proposal. It only reads (no auth, like `/api/status`). Start and abort MUST
  call `requireAuth` and need the command header; a refusal is 409 with `reason` and `error`.
- **FR-021**: The status document's roof section MUST report `calibrating` (true while a run is
  active), and the Stop bar MUST show while it is true.

**Web UI**

- **FR-022**: A page at `/calibrate` MUST show what the run needs and whether each holds now
  (weather, park, fault, roof state, close sequence), the start form (the "I am at the roof and
  can see it" confirmation, where the roof is, the travel, the top-duty pass when available), and
  Start, disabled with its reason when a check fails. While a run is active it MUST show the leg,
  its elapsed time against its limit, the live Hall reading, and a Stop button that is never
  disabled. At the end it MUST show the ending, the legs, the readings and the proposal beside the
  saved values, with Accept and save (through the settings save) and Discard.
- **FR-023**: Settings → Roof MUST have a Calibrate card with `roof.calibrationLimitSeconds` and a
  link to the run's page. The setup wizard MUST have a Calibration run page after the Hall page
  that explains the run and links to the run's page, with no motion control of its own.
- **FR-024**: Every new text MUST go through i18n with its context note; the pages MUST work at
  phone width and pass the accessibility scan.

**Setting, demo, docs**

- **FR-025**: `roof.calibrationLimitSeconds` (default 180, 30-600, whole seconds) MUST be in the
  config model, the stored settings, the web schemas, the shared range fixture and the settings
  reference. Older stored settings load with the default.
- **FR-026**: The demo's simulated roof MUST support the run end to end (start, legs, stop,
  result, accept) through the same library.
- **FR-027**: The commissioning guide's stages 3 and 8 and its checklist MUST use the run; the
  quick bench test MUST say the run needs real magnets (or the potentiometer stand-in); the REST
  reference, the configuration reference, the calibration page and the OpenAPI document MUST
  describe it.

### Key Entities

- **Calibration run**: its state, its request (where the roof was, the travel, the top-duty
  pass), its legs, its readings, its ending and its proposal. One per device, in memory.
- **Leg**: a direction, a duty profile, a time limit, the level it must end on (or any), and what
  it measured (time driven, duty-weighted time, end level, current, supply).
- **Proposal**: the proposed travel, speed, timeout margin, high end and thresholds, the saved
  values beside them, and the reason for any value withheld.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (the page's parts are cards; the Stop bar stays outside the grid), DS-02
  (each precondition's state is a pill), DS-03 (readings are `ReadingRow`s; the live Hall reading
  uses the existing scale), DS-04 (explanations behind "?"), DS-06 (the run's progress and outcome
  in the card that started it), DS-07 (a lasting problem is a Note), DS-09 (phone width), DS-20 to
  DS-28 (copy). Only the shared parts in `ui.tsx` and `settings/controls.tsx`; no new button or
  card styles (constitution VII). Stop uses the existing danger button.
- **Mockup**: not reviewed. The owner was not available; the page is built from existing cards,
  rows, notes and controls only. The owner reviews it in the demo before merging (listed under the
  PR's pending checks).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In the demo, a run from the closed end with the default settings finishes in under
  5 minutes of device time and, with the travel entered as the distance between the magnets' trigger points, proposes a speed within 2 % of the simulated roof's.
- **SC-002**: In the native tests, every leg of every simulated run stops within its limit, and
  each plant fault (missing magnet, jam, backwards motor, low supply, over-current, rail, park
  lost, weather turned) ends the run with no proposal and the existing fault or reason.
- **SC-003**: No path but the authenticated start route can start a run (tested for Alpaca, MQTT,
  the automatic close and a start without `attended`).
- **SC-004**: A stopped, failed or ended run leaves every setting byte-for-byte unchanged
  (tested), and a restart leaves no run active.
- **SC-005**: The full local check (firmware build, native tests, web tests, Playwright with the
  accessibility scan at phone width, quality gate, docs build, flash and UI budgets) passes with no
  new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Refused in unsafe weather**, although the first leg may be a close: the run has to open, and
   the close sequence already closes the roof in bad weather.
2. **The mount must be parked for the whole run**, confirmed at the start and at every close leg,
   with the existing interlock during each close.
3. **The person says where the roof is.** The run can't trust the polarity it is measuring, so it
   asks, checks the answer against the Hall reading, and tells the ends apart by the direction
   driven. A wrong answer at an end pushes into an end stop for at most the leave time.
4. **The run needs thresholds that already see both magnets.** The sensor only sees the ends, so
   the run can't find a better stopping point than the one the current thresholds give; it keeps
   each threshold or moves it only as far as the noise or the reading at rest requires, never
   further toward the end stop. Rough thresholds come from the Hall page's live reading first.
5. **The travel is the person's tape measure**, the current setting by default; the run proposes
   the speed that fits it.
6. **The time limit is a new setting** (180 s, 30-600 s): about 1.5 times a slow travel with the
   default roof. Later legs are bounded by the measured ones. During a leg the drive budget is the
   leg's limit, since the old settings' budget may be wrong.
7. **Top-duty legs slow down early**: at half the time the slow leg predicts at the top duty,
   allowing a motor deadband of up to half the slow duty, less the ramp-down time, so they arrive
   at the slow duty even when the motor is much faster at the top duty than the slow leg suggests.
   The top-duty pass then spends much of its time at the slow duty; the speed is still measured
   from the duty actually applied.
8. **The speed comes from the top-duty legs when run**: real moves spend most of their time at the
   top duty. The margin covers the slow legs' shortfall, so a move from an unknown position (at
   the slow duty) still fits its timeout.
9. **Disagreeing legs (over 30 %) withhold the speed and margin**: that much difference means
   something slipped or a magnet was read early.
10. **Current limits are not proposed**, only shown, with a warning at 80 % of the limit; they
    are a judgement about the motor and the driver.
11. **Accept goes through the settings save** in the web UI; the device stores no proposal and
    has no "apply" route.
12. **The automatic close always wins**, ending the run; a closing leg carries on as its close.
13. **Everything is held for the whole run**, pauses included.
14. **The status route is open**, like `/api/status`: readings and derived values only.
15. **The wizard gets an eleventh page** after the Hall page. A saved `setup.step` from before
    this release then resumes one page earlier than it did, which only repeats a page.
16. **The person confirms the open end** after the slow open. The run can't tell a motor that runs
    the other way from part-way (both ends then look consistent), so it asks; "No", or no answer
    within 5 minutes, ends it with nothing proposed.
17. **The automatic close keeps a closing leg running** as its close rather than stopping it: the
    leg drives the close direction to the first magnet within its limit, like a person's close from
    an unknown position, which the automatic close itself may not start (constitution I). Stopping
    it would leave a roof that started part-way open in bad weather.
18. **"Authenticated" is the device's login**: with password protection off, as for every roof
    command, anyone on the trusted LAN can start a run (constitution VIII), still needing the
    attended confirmation and the command header.
19. **MQTT reconnects wait for the run** like every platform action while the roof moves; stale
    MQTT weather then ends the run (fail-safe).
20. **The UI mockup review is pending**: the owner reviews the page in the demo before merging.

## Out of scope

- A per-move telemetry log or per-sample export (spec 017).
- Proposing ramps, duties, the leave time or the current limits.
- Any run that isn't started by a person at the roof; resuming a run after a restart.
- Measuring the travel (the sensor can't).
