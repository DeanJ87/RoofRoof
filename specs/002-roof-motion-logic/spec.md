# Feature Specification: Roof State and Motion Logic

**Feature Branch**: `spec/002-roof-motion-logic`

**Created**: 2026-11-11

**Status**: Draft

## Context

The roof is moved by a 12 V worm-gear motor through an IBT-2 H-bridge. The firmware knows where
the roof is only from one linear Hall sensor reading two magnets, one at each end, and from the
time it has been moving. Spec 001 holds the driver off and decides the driver's pin levels
(`lib/DriverLogic`). This spec adds the decisions that choose *when and how* to drive it:

- the roof's state (closed, open, opening, closing, stopped part-way, error, unknown);
- the open, close, stop and abort commands and when each is refused;
- the speed profile of a move (soft start, slowing near the end);
- when a move ends (the target magnet) and when it is a fault (timeout, jam, wrong magnet, sensor,
  supply, a late control loop);
- the position estimate between the magnets;
- the fault latch.

It is pure logic with native tests (constitution I and V). Readings come in as plain values and
the result is a driver command and a duty. Nothing here reads a pin or runs on a timer: spec 003
feeds it from the board and applies its output. The settings it needs are added to the settings
model so later specs can show and change them.

Nothing in this spec can move the roof on the device: no firmware code calls the new logic until
spec 003.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: What is the difference between Stop and Abort for the position? → A: Stop keeps the time-based
  estimate (state "stopped"); abort part-way makes the position unknown (constitution I names
  abort). Both switch the driver off on the step.
- Q: Does a low supply at rest latch a fault? → A: No. At rest it refuses open and close with the
  voltage and the limit; during a move it is a latched fault.
- Q: What happens to a command for the other direction during a move? → A: The move stops, the
  driver stays off for the dead time, then the new move starts with its own timeout (ASCOM allows
  CloseShutter during OpenShutter). It is refused like any other move if the position is unknown and
  the origin is automatic.
- Q: In what units does the Hall reading arrive? → A: Raw 12-bit ADC counts (0-4095), so the
  thresholds match the live reading spec 009 shows for calibration; spec 003 does no scaling.
- Q: Which wins when the target magnet and an electrical fault (over-current, supply) are seen on
  the same step? → A: The magnet: the move has ended at its end and the driver is off either way.
  A wrong magnet or a Hall rail is still checked first.

## Constitution rules this spec relies on

Principle I (Motion Safety) and the tests that prove each rule are listed in
[plan.md](plan.md#constitution-check) and [contracts/roof-logic.md](contracts/roof-logic.md).
Principles II (park interlock) and III (weather) act on top of this logic in spec 005; this spec
gives them a way to stop a move and latch a fault, nothing more.

## User Scenarios & Testing *(mandatory)*

The "user" of this logic is the owner at the roof, a client acting for them (N.I.N.A., Home
Assistant), and the automatic close of spec 005. Each story is proven by native tests that feed
the logic simulated readings and time.

### User Story 1 - Open and close end to end (Priority: P1)

The owner asks the closed roof to open. The motor starts gently, runs at the set speed, slows as
the estimated position nears the open end and stops when the open magnet is read. The roof reports
"open". Closing works the same way in reverse.

**Why this priority**: This is the controller's job. Every later spec (hardware glue, close on
unsafe, Alpaca, MQTT, dashboard) drives the roof through it.

**Independent Test**: A simulated roof (position from the duty and the set speed, the Hall reading
from the position) runs an open and a close; the logic ends each at the right magnet with the
state, the position and the driver output checked at every step.

**Acceptance Scenarios**:

1. **Given** the roof closed (the closed magnet read), **When** a person or a client asks it to
   open, **Then** the driver turns in the open direction, the duty rises from the slow duty to the
   top duty over the ramp-up time, and the state is "opening".
2. **Given** the roof opening, **When** the estimated distance left is inside the slow-down zone,
   **Then** the duty falls toward the slow duty, and it never rises above the top duty.
3. **Given** the roof opening, **When** the open magnet is read, **Then** the driver is switched
   off on that step, the state is "open" and the position is the full travel.
4. **Given** the roof open, **When** asked to close, **Then** the same happens in the other
   direction and it ends "closed" at position 0.
5. **Given** the roof already open, **When** asked to open, **Then** the command is accepted and
   nothing moves.
6. **Given** a move under way, **When** the same command arrives again, **Then** the move carries
   on unchanged.

---

### User Story 2 - Every fault stops the motor and stays stopped (Priority: P1)

Something goes wrong in a move: the magnet never arrives, the motor stalls, the sensor
disconnects, the supply sags, or the control loop runs late. The motor stops on that step, the
driver is switched off, and no move is possible until a person clears the fault. A restart does not
clear it.

**Why this priority**: Constitution I. A jammed or blind roof that keeps pushing can crush a
telescope or burn out the motor.

**Independent Test**: For each fault, a simulated move is driven into it; the driver output is off
from that step on, every later command is refused, and only a person's "clear fault" ends it.

**Acceptance Scenarios**:

1. **Given** a move, **When** its time reaches the expected travel time plus the margin without
   the target magnet, **Then** it stops with a timeout fault stating the elapsed time and the limit.
2. **Given** a move from a magnet, **When** the roof hasn't left that magnet within the leave time,
   **Then** it stops with a "didn't leave the magnet" fault (motor wired backwards, or jammed at the
   end).
3. **Given** a move, **When** the magnet of the other end is read (or the start magnet again after
   leaving it), **Then** it stops with a wrong-magnet fault.
4. **Given** current sense on, **When** the motor current stays at or above its limit for the
   confirm time, **Then** it stops with an over-current fault stating the current and the limit;
   with current sense on and no current reading, it stops with a current-sensor fault.
5. **Given** a move, **When** the supply stays below its minimum (or can't be read) for the confirm
   time, **Then** it stops with a supply fault stating the voltage and the limit.
6. **Given** any state, **When** the Hall reading sits at either rail of its range, **Then** a Hall
   sensor fault is latched (the sensor is disconnected or shorted).
7. **Given** a move, **When** the gap since the previous control step is longer than the allowed
   gap, **Then** it stops with a late-loop fault.
8. **Given** a latched fault, **When** any open or close arrives, from any origin, **Then** it is
   refused with the fault as the reason, and the driver stays off.
9. **Given** a latched fault, **When** an automatic origin asks to clear it, **Then** it is refused;
   **When** a person clears it, **Then** it clears, and the position is known only if a magnet is
   read.
10. **Given** a fault restored at start-up (as saved by the firmware), **Then** the logic starts
   latched with that fault.

---

### User Story 3 - Never move by itself from an unknown position (Priority: P1)

After a start-up between the magnets, after an abort or after a fault, the logic does not know
where the roof is. An automatic close is refused. A person can still move the roof, slowly, and it
stops at the first magnet it reaches.

**Why this priority**: Constitution I. Without a position, the timing estimate and the slow-down
zone mean nothing, so only a slow, attended move is allowed.

**Independent Test**: Start the logic with the Hall reading between the thresholds. Every automatic
command is refused; a person's command moves at no more than the slow duty and ends at the first
magnet, or faults on the wrong one.

**Acceptance Scenarios**:

1. **Given** a start-up with the reading between the thresholds, **Then** the state is "unknown"
   and the driver is off, whatever the elapsed time.
2. **Given** the state unknown, **When** an automatic open or close arrives, **Then** it is
   refused with "position unknown".
3. **Given** the state unknown, **When** a person asks to close, **Then** the roof moves toward
   closed at no more than the slow duty, with a timeout of the whole travel at that duty plus the
   margin, and stops at the closed magnet.
4. **Given** the state unknown and a person's move, **When** the other end's magnet is read,
   **Then** it stops with a wrong-magnet fault.
5. **Given** a start-up at a magnet, **Then** the state is that end and moves need no person.
6. **Given** a move, **When** it is aborted, **Then** the driver is off on that step and the
   position is unknown unless a magnet is read.

---

### User Story 4 - Stop part-way and carry on (Priority: P2)

The owner stops the roof half open to clear something. The roof reports "stopped" with its
estimated position. A later open or close continues from that estimate, slowing near the end as
usual.

**Why this priority**: Useful, but the roof works without it (US1 to US3).

**Independent Test**: Stop a simulated move part-way; the state and the estimate are reported;
resume in either direction and it ends at the right magnet.

**Acceptance Scenarios**:

1. **Given** a move, **When** Stop arrives, **Then** the driver is off on that step and the state
   is "stopped" with the estimated position.
2. **Given** "stopped", **When** open or close arrives (from a person or a client), **Then** the
   roof moves from the estimate with the normal profile and a timeout for the distance left.
3. **Given** a move, **When** a command for the other direction arrives, **Then** the driver is
   off for at least the dead time before it turns the other way.
4. **Given** any stop, **When** a new move starts, **Then** the driver stays off for the dead time
   first.

---

### User Story 5 - The roof's behaviour is set by settings (Priority: P2)

The owner (or spec 009's settings page later) sets the measured travel and speed, the timeout
margin, the ramps and duties, the Hall polarity and thresholds, current sense and the supply limit.
Settings are validated the same way by the device and the web UI and saved with the rest.

**Why this priority**: The roof's real numbers are measured once it is built, so hardware
facts are settings with conservative defaults.

**Independent Test**: Load, save, validate and round-trip the roof settings through the settings
JSON on the device and in the web schema, with the shared range fixture.

**Acceptance Scenarios**:

1. **Given** settings saved by spec 001's firmware (no roof section), **When** loaded, **Then**
   the roof settings take their defaults and current sense stays off.
2. **Given** a value outside its range, or a slow duty above the top duty, or Hall thresholds in
   the wrong order or too close, **When** saved, **Then** the device and the web UI reject it with
   the same message.
3. **Given** the roof moving, **When** the logic is handed new settings, **Then** it refuses them
   and keeps the ones it is moving with.

### Edge Cases

- The millisecond clock wraps (after 49.7 days) during a move: elapsed times are still right.
- The first control step: no gap to check; the Hall reading is not yet settled, so moves are
  refused until it is.
- A noisy Hall reading at a threshold: a zone change needs several readings in a row, and leaving
  a magnet needs the reading past the threshold by a hysteresis band.
- The reading leaves a magnet while the roof is idle (sensor drift, or the roof moved by hand):
  the state becomes "unknown".
- Stop while waiting out the dead time: nothing was driving; the state goes back to what it was
  before the command. Abort at that point makes the position unknown unless a magnet is read, as
  the roof may have moved before the pause.
- Open while closing (or the reverse): the motor stops, waits the dead time and turns the other
  way; the timeout is for the new move.
- Current sense on and the motor not yet driven (dead time): current is not checked.
- Settings with a speed so low or a travel so long that a timeout would overflow: the settings
  ranges keep every computed time well inside 32 bits.
- Supply low while idle: an open or close is refused (with the voltage and the limit) but no fault
  is latched, since nothing moved.

## Requirements *(mandatory)*

### Functional Requirements

**States and commands**

- **FR-001**: The logic MUST report one of seven states: unknown, closed, open, opening, closing,
  stopped (part-way) and error (a fault is latched).
- **FR-002**: The logic MUST accept four commands, each with an origin (a person, or automatic):
  open, close, stop and abort. Stop and abort MUST always be accepted, from any origin, in any
  state. Commands MUST carry no time: every time the logic uses comes from its control steps. Abort
  with no move under way changes nothing (a stopped roof keeps its estimate, since nothing moved).
- **FR-003**: Every open or close MUST return accepted or refused; a refusal MUST carry the reason
  and, where there is one, the measured value and the limit (constitution III).
- **FR-004**: Open or close MUST be refused while a fault is latched, while the Hall reading has not
  settled, while the supply is below its minimum or can't be read, while current sense is on and
  the current can't be read, and while the logic's settings are invalid.
- **FR-005**: With the position unknown, open or close MUST be refused unless the origin is a
  person.

**Driving**

- **FR-006**: The logic's output MUST be a driver command (off, forward, reverse) and a duty
  (0-255) for `lib/DriverLogic`. It MUST be off whenever no move is being driven.
- **FR-007**: Which driver direction opens the roof MUST be a setting.
- **FR-008**: Before the driver turns on after any stop, and after start-up, it MUST stay off for
  a dead time of at least 500 ms, including when a move is reversed. The dead time MUST be timed by
  control steps only, from the first step after the driver went off, so no caller's clock can
  shorten it, and a step clock that goes backwards MUST count as still inside the dead time.
- **FR-009**: The duty MUST never exceed the top duty setting; during a move from an unknown
  position it MUST never exceed the slow duty setting.
- **FR-010**: A move from a known position MUST start at the slow duty and rise linearly to the
  top duty over the ramp-up time, then fall toward the slow duty as the estimated distance left
  enters the slow-down zone (the distance covered at top duty in the ramp-down time). After the
  estimate reaches the end, it MUST hold the slow duty until the magnet or the timeout.
- **FR-011**: A move MUST end on the step its target magnet is read: driver off, state open or
  closed, position the full travel or 0.

**Position**

- **FR-012**: The position MUST be known (0 at closed, the travel at open) while a magnet is read,
  estimated from time and duty (at the set speed, scaled by the duty) during a move from a known
  position and after Stop, and unknown otherwise: at start-up between the magnets, during and after
  a move from an unknown position until a magnet is read, after an abort part-way, after a fault,
  and when the reading leaves a magnet while idle.
- **FR-013**: The estimate MUST stay between 0 and the travel.

**Timeouts and faults**

- **FR-014**: Every move MUST have a hard timeout: the expected time of its profile for the
  distance to go plus the margin setting; for a move from an unknown position, the whole travel at
  the slow duty plus the margin. The time counts from when the driver turns on.
- **FR-015**: A move that starts at a magnet MUST leave it within the leave time setting.
- **FR-016**: These MUST be motion faults: timeout; didn't leave the start magnet; wrong magnet
  (the other end's, or the start magnet again after leaving it); Hall reading at a rail; over-current
  for the confirm time (current sense on); no current reading (current sense on); supply below its
  minimum or unreadable for the confirm time during a move; a control step later than the allowed
  gap during a move; the drive budget used up (FR-025); and a fault raised from outside (the park
  interlock of spec 005, the watchdog of spec 003).
- **FR-017**: A fault MUST switch the driver off on the step it is detected, latch, make the state
  "error" and the position unknown, and record the fault kind, the measured value and the limit.
- **FR-018**: A latched fault MUST be cleared only by a person's command. The logic MUST accept a
  fault restored at start-up so a restart doesn't clear it (saving it is spec 003's).
- **FR-019**: Three rail readings among the last eight, consecutive or not, MUST latch a fault
  whether or not the roof is moving; fewer are a glitch and MUST NOT interrupt the confirmation of a
  magnet.

**Hall sensor**

- **FR-020**: A reading at or above the high threshold MUST count as the magnet of the end set as
  high, a reading at or below the low threshold as the other end's, and anything between as no
  magnet. A zone change MUST take three readings in a row, and leaving a magnet MUST need the
  reading past its threshold by 40 counts.

**Settings**

- **FR-021**: The settings model MUST hold, with these defaults: travel 1850 mm, speed 38 mm/s,
  timeout margin 15 s, leave time 10 s, ramp-up 2000 ms, ramp-down 3000 ms, top duty 100%, slow
  duty 40%, open direction forward, high end open, high threshold 3000 and low threshold 1100
  counts, current sense off, current limit 10 A, supply minimum 11.0 V.
- **FR-022**: Each roof setting MUST be range-checked with the same rules and messages on the
  device and in the web UI (`test/fixtures/config-ranges.json`), including slow duty <= top duty and
  the Hall thresholds in order, apart by at least two hysteresis bands and inside the rails.
- **FR-023**: Settings saved before this spec MUST load with the roof defaults, and no migration
  may turn current sense on.
- **FR-024**: The logic MUST refuse new settings while a move is being driven.

**Added after the safety review**

- **FR-025**: The time driven toward each end since a magnet was last read (or a person last
  cleared a fault) MUST NOT reach the drive budget: the longest single move, a whole travel at the
  slow duty plus the margin. The budget is kept per direction, so one reversal part-way (nearly two
  travels of driving between magnets) never trips it, while repeated restarts of a jammed roof in
  one direction with current sense off still latch a fault; alternating directions is bounded at
  twice the budget. Settings taken at rest can make the budget smaller than what is already used;
  the next drive then faults on its first step (fail-safe, accepted).
- **FR-026**: A supply reading that is not finite or is above 16 V MUST count as unreadable, like
  NaN. With current sense on, a current reading that is not finite or is below -0.5 A MUST count as
  unreadable.

### Key Entities

- **Roof settings**: travel, speed, margins, ramps, duties, direction, Hall polarity and thresholds,
  current sense, supply minimum. Persisted with the rest of the config.
- **Readings**: the time, the Hall reading (ADC counts 0-4095), the supply (volts) and the motor
  current (amps, or none).
- **Roof status**: the state, the position (or unknown), the target of the move, the latched fault
  and the elapsed time and timeout of the move.
- **Fault**: a kind, the measured value, the limit.
- **Refusal**: a reason, the measured value, the limit.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Across at least 10,000 randomly generated sequences of commands, origins and
  readings, the logic never asks for both directions at once, never asks for a duty above the top
  duty (or above the slow duty in a move from unknown), never drives while a fault is latched, and
  never starts a move from an unknown position without a person's command. They also keep the
  driver off for the dead time after any stop and after start-up, latch a rail pattern of three in
  eight readings, keep the total driving without a magnet under the drive budget, and end every
  move at its target magnet. Each of these properties is shown to catch a deliberately broken
  version of the logic.
- **SC-002**: Every fault kind and every timeout is shown by a test to switch the driver off on the
  step it occurs, at its boundary (one step before: still driving; at the limit: stopped).
- **SC-003**: A simulated open and close with the default settings each end at their magnet within
  the timeout, with the roof never driven after the magnet is read.
- **SC-004**: Every roof setting's range is accepted at its bounds and rejected just outside them by
  both the device and the web UI.
- **SC-005**: The full local check (firmware build, native tests, web tests, quality gate, docs
  build, flash budget) passes with no new warnings.

## Assumptions decided for the owner

Each is the safest option the constitution allows; the owner can change any of them later.

- **Stop keeps the estimate, abort drops it.** Stop leaves the state "stopped" with the time-based
  estimate, so open or close can carry on from it (the constitution lists abort, not stop, as making
  the position unknown). Alpaca's AbortSlew (spec 006) should map to abort.
- **A move from "stopped" uses the normal profile** with a timeout for the distance left. If the
  estimate is wrong, the move ends at the magnet or faults on the timeout; it never runs on.
- **Supply low while idle refuses, during a move faults.** Nothing moves, so latching a fault for
  a dip at rest would only block the automatic close later.
- **A Hall rail reading latches a fault even at rest.** A disconnected sensor needs a person.
- **Reversing goes through stop and the dead time** rather than being refused, as ASCOM's Dome
  allows CloseShutter during OpenShutter.
- **The motor's speed is taken as proportional to the duty** for the estimate and the expected
  time. The margin covers the difference; the bench check measures it.
- **Fixed values** (not settings, as they describe the ADC and the control loop, not the roof):
  dead time 500 ms, Hall confirmation 3 readings, Hall hysteresis 40 counts, Hall rails at 30 and
  4065 counts, over-current and supply confirm time 250 ms, the allowed control-step gap 100 ms
  (spec 003 steps the logic every 20 ms or faster), a rail confirmed by 3 rail readings among the
  last 8, a supply reading above 16 V or a current reading below -0.5 A treated as unreadable.
- **Commands carry no time.** The dead time, timeouts and estimate use only the control steps'
  clock, so a command from another task (web server, MQTT, Alpaca) with a skewed timestamp can't
  shorten the dead time. Start-up counts as a stop, so the first drive also waits the dead time.
- **The drive budget is the longest single move, per direction** (a whole travel at the slow duty
  plus the margin) and is reset by reading any magnet or by a person clearing a fault. A single
  shared budget would trip on a legitimate reversal near an end and block the close on rain. A roof that is stopped
  and restarted part-way many times without reaching a magnet latches `driveBudget` rather than
  getting a fresh timeout each time.
- **Abort with no move under way keeps the position.** Nothing moved, so Alpaca's AbortSlew on a
  stopped roof leaves its estimate.
- **Added settings beyond the roadmap's list**: the open direction (so a reversed motor is a
  setting, not a rewire), the leave time (so a reversed motor or a jam at an end is caught in
  seconds rather than at the full timeout), and the supply minimum (the low-supply fault is
  decided here; spec 003 measures the voltage).
- **The default thresholds are deliberately far from the sensor's resting value** (3000 and 1100
  counts around a ~2000 resting reading): an uncalibrated sensor then fails as "no magnet before the
  timeout" (safe) rather than seeing a magnet that isn't there.
- **Fault and refusal wording** is left to the specs that show it (003 status, 007 alerts); this
  spec provides the kind, the measured value and the limit, and stable machine names.

## Out of scope

- Reading the pins and the ADC, the control task and its rate, saving the latched fault in NVS, the
  boot sequence and the REST commands (spec 003).
- The safety sources (004), the close-on-unsafe sequence and the park interlock (005), Alpaca (006),
  alert events (007), MQTT and Home Assistant (008), the dashboard and settings page (009).
