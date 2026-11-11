# Feature Specification: Roof Hardware

**Feature Branch**: `spec/003-roof-hardware`

**Created**: 2026-11-11

**Status**: Draft

## Context

Spec 002 decides what the roof does (`lib/RoofLogic`): its state, every motion fault, the fault
latch and the driver command for each control step. Spec 004 decides whether the weather is safe
and whether the mount is parked (`lib/SafetyInputLogic`). Neither touches a pin. Spec 001 only holds
the motor driver off.

This spec is the glue that runs the roof on the board:

- reading the Hall sensor, the supply voltage and (when fitted) the motor current from the ADC, and
  the park switches and the rain contact from their GPIO inputs;
- stepping the roof logic at a fixed rate in its own task and applying its output to the motor
  driver;
- a watchdog that switches the driver off if the stepping stalls;
- keeping a latched fault across restarts;
- the roof's readings and state in the status document and the live updates;
- authenticated REST commands to open, close, stop and clear a fault;
- the wiring page and a calibration procedure.

After this spec the roof can move on a person's command. Every move goes through the motion logic
of spec 002 and through the park and weather checks of the constitution (II, III). The automatic
close on unsafe weather is spec 005; Alpaca, MQTT and the dashboard card are later specs.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Spec 005 owns the close sequence and the park interlock. Does a close from this spec's REST
  command check the park? → A: Yes. Constitution II says every close is checked, whatever its
  source, so a close is refused unless the park is confirmed (N.I.N.A. at park AND every fitted park
  switch closed), and a close in progress stops as a motion fault if the park confirmation is lost.
  Spec 005 builds its sequence on top of the same check.
- Q: Is an open refused while the weather is not safe, before spec 005 exists? → A: Yes
  (constitution III: opening is refused while the verdict is unsafe). Only a "safe" verdict allows
  an open, and an open in progress stops (not a fault) as soon as the verdict is no longer safe.
- Q: Which way round do the park switches and the rain contact read? → A: Fixed, not settings: each
  is a contact to ground with a pull-up, so a closed contact reads low. A closed park switch means
  parked; a closed rain contact means rain. The inverted park wiring would read a broken wire as
  "parked", so it is not offered.
- Q: How is the motor current scaled? → A: By a new setting, amps per volt at the driver's
  current-sense output (default 8.5 A/V for the BTS7960's typical ratio into the IBT-2's 1 kΩ),
  because the sense ratio is a hardware fact that varies between boards. The divider ratios of the
  supply (100k/22k) and current (10k/20k) are fixed by the constitution's pin table.
- Q: Which settings saves are refused while the roof moves? → A: Any save that changes a roof
  setting or a park setting, from the settings page or a restore, while the roof is opening or
  closing. The save is refused with the reason; nothing is half applied.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | Boot sets EN low and both PWM low before anything else | `MotorDriver::holdOff()` (spec 001), unchanged |
| I | RPWM and LPWM never together; one module writes the pins | `lib/DriverLogic` decides, `MotorDriver` applies (FR-008) |
| I | Never move by itself with the position unknown; unknown after a boot between the magnets | spec 002's controller; FR-010 here feeds it nothing but readings |
| I | A fault stops, drops EN, locks out; a restart never clears it | spec 002's latch; FR-011, FR-012 here keep it across restarts |
| I | Motion runs in its own task at a fixed rate; a missed deadline stops the motor | FR-005 to FR-007 |
| I | Settings that change motion are refused while moving | FR-016 |
| II | Close only onto a confirmed park; the interlock is re-checked throughout the close | FR-017, FR-018 |
| III | Opening is refused while the verdict is unsafe | FR-019 |
| III | Every fault and refusal states the measured value and the limit | FR-022 |
| VIII | Every endpoint that moves the roof or clears a fault calls `requireAuth` | FR-020 |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

The users are the owner at the roof or on the LAN, and the clients acting for them through the REST
API. Pure decisions are proven by native tests; the glue is proven on the bench (see the PR's
bench-check list).

### User Story 1 - Move the roof from the API, safely (Priority: P1)

The owner sends an authenticated open. The firmware checks the weather verdict, hands the command
to the motion logic and the roof opens, ending on its magnet. The owner can stop it at any time.
Close works the same way, and only onto a parked mount.

**Why this priority**: This is what the hardware glue is for: the first time the roof moves under
the firmware's control.

**Independent Test**: The pure gate rules (weather, park, interlock) are tested natively against
every verdict and park result; on the bench, an open and a close end on their magnets.

**Acceptance Scenarios**:

1. **Given** the roof closed, the weather verdict safe and a person authenticated, **When** they
   send open, **Then** the command is accepted, the roof opens through spec 002's profile and ends
   "open" on the open magnet.
2. **Given** the weather verdict unsafe or unknown, **When** open is sent, **Then** it is refused
   with the verdict's reasons and nothing moves.
3. **Given** the roof open, **When** close is sent and the park is not confirmed, **Then** it is
   refused with the park result's reasons and nothing moves.
4. **Given** the park confirmed, **When** close is sent, **Then** the roof closes and ends "closed".
5. **Given** a close in progress, **When** the park confirmation is lost (a fitted park switch
   opens, or N.I.N.A. no longer reports the mount parked), **Then** the motor stops as a motion
   fault stating why.
6. **Given** an open in progress, **When** the weather verdict stops being safe, **Then** the roof
   stops (driver off, position estimate kept) and the log says why.
7. **Given** any state, **When** stop is sent, **Then** it is accepted and the driver is off at
   once.
8. **Given** no credentials while password protection is on, **When** any roof command is sent,
   **Then** it is refused with 401 and nothing changes.

---

### User Story 2 - The motor never runs unsupervised (Priority: P1)

The roof logic is stepped at a fixed rate by its own task, independent of Wi-Fi, the web server and
MQTT. If the task stops stepping while the driver is on, a watchdog switches the driver off and a
fault is latched.

**Why this priority**: Constitution I. A busy network or a crashed task must never prolong a move.

**Independent Test**: The watchdog rule (the gap since the last step against its limit, wrap-safe)
is tested natively; on the bench, a debug build that blocks the task mid-move shows EN falling
within the limit.

**Acceptance Scenarios**:

1. **Given** the roof moving, **Then** the logic is stepped every 20 ms, and the output is applied
   after every step and every command.
2. **Given** the driver on, **When** no step has run for longer than 100 ms, **Then** the watchdog
   switches EN off without waiting for the task, and the next step latches a watchdog fault with
   the gap and the limit.
3. **Given** the driver off, **When** the task is late, **Then** nothing is latched (nothing was
   moving), and the logic's own late-step check covers a late step while driving.
4. **Given** the watchdog has switched the driver off, **When** the task tries to switch it back
   on before the fault is latched, **Then** EN stays off.

---

### User Story 3 - A fault survives a restart (Priority: P1)

A motion fault latched before a restart, a crash or a power cut is still latched after it, with the
same kind, value and limit. Only a person clears it.

**Why this priority**: Constitution I: "A restart never clears the lockout."

**Independent Test**: The stored fault record's encoding and decoding are tested natively, including
an empty store, a damaged record and every fault kind; on the bench, a fault survives a power cycle.

**Acceptance Scenarios**:

1. **Given** a fault latched, **Then** it is saved within one control step of being latched.
2. **Given** a saved fault, **When** the device starts, **Then** the fault is latched before the
   first control step, and the roof can't move until a person clears it.
3. **Given** a stored record that can't be read back (damaged, or an unknown kind), **When** the
   device starts, **Then** a fault is latched saying the stored fault was unreadable.
4. **Given** a person clears the fault, **Then** the stored record is removed, and the next start
   has no fault.
5. **Given** nothing stored (a new device or one that has never faulted), **Then** the device starts
   with no fault, and the state follows the Hall reading: at a magnet it is that end, between the
   magnets it is unknown, and nothing moves.

---

### User Story 4 - See the roof's readings and state (Priority: P2)

The owner (and later the dashboard, MQTT and Home Assistant) reads the roof's state, position, the
raw Hall reading, the supply voltage, the motor current, the park switch and rain contact readings
and any fault from the status document and the live updates.

**Why this priority**: Needed for calibration and for every later spec, but the roof works
without it.

**Independent Test**: The status section is built by a pure function and tested natively for every
state, an unknown position, unreadable readings and a fault; the demo's emulated status matches the
contract schema.

**Acceptance Scenarios**:

1. **Given** any state, **Then** `/api/status` and `/ws/status` carry a `roof` section with the
   state, whether the position is known and the position in mm and percent, the move's target,
   elapsed time and timeout, the Hall level and raw counts, the supply voltage and whether it is
   below its minimum, the motor current (or null when current sense is off), the park switch and
   rain readings, the latched fault with its text, and the watchdog's count of trips since start.
2. **Given** a reading that can't be taken, **Then** it is null, never a made-up number.
3. **Given** the state changes, **Then** a live update goes out within 250 ms, not only on the
   regular 2 s broadcast.

---

### User Story 5 - Clear a fault (Priority: P2)

After fixing what caused a fault, the owner sends an authenticated clear-fault. The fault clears,
the stored record is removed, and the position is known again only if a magnet is read.

**Why this priority**: Without it, a fault needs a reflash. It is P2 because it only follows a
fault.

**Independent Test**: The command path is spec 002's (tested there); the stored record's removal
is tested with the fault store's rules; on the bench, clear-fault after a forced fault.

**Acceptance Scenarios**:

1. **Given** a fault latched, **When** an authenticated clear-fault is sent, **Then** the fault
   clears, the state follows the Hall reading, and the stored record is removed.
2. **Given** no fault, **When** clear-fault is sent, **Then** it is accepted and nothing changes.

---

### User Story 6 - Wire and calibrate the roof (Priority: P2)

The owner wires the board from the wiring page and calibrates the Hall thresholds, the supply
reading and (when fitted) the current sense with the calibration procedure, using the live readings
in the status document.

**Why this priority**: The roof's numbers are settings measured once it is built.

**Independent Test**: The docs build passes in strict mode; the wiring page matches the pin table
and `include/RoofPins.h`.

**Acceptance Scenarios**:

1. **Given** the wiring page, **Then** it lists every pin, divider, pull-up and the polarity of the
   park switches and rain contact as the firmware reads them.
2. **Given** the calibration page, **When** the owner follows it with the motor disconnected,
   **Then** they can read the Hall counts at each magnet and between them, set the thresholds and
   polarity, and check the supply and current readings against a meter.

### Edge Cases

- The millisecond clock wraps during a move: the watchdog and step gaps use wrap-safe differences.
- A park switch or rain contact that chatters (a bad contact) never settles: it reads "unreadable"
  after one second without a steady level, which is "not parked" and "unsafe".
- At start-up the switch and contact inputs read "unreadable" until they have settled (three equal
  readings), so a close can't start in the first steps after a boot.
- A command arrives from the web server while a step is running: commands and steps are serialised,
  and the output of whichever ran last is applied.
- A settings save that passes the "not moving" check just before a move starts: the motion logic
  refuses new settings while moving, and the task hands them over when the move ends.
- The supply sense reads 0 V (divider disconnected): the supply is low, open and close are refused
  and a move in progress faults (spec 002).
- The Hall output disconnected: the divider pulls GPIO39 to 0, a rail reading that latches a Hall
  fault (spec 002).
- Current sense enabled in the settings but not wired: the reading is near 0 A, which is not a
  fault; the bench check confirms the reading before enabling it.
- Saving the fault record fails (NVS full or worn): the failure is logged as an error and the fault
  stays latched in memory; the save is retried every 5 s.
- A clear-fault while the fault record save is pending: the latest state wins.

## Requirements *(mandatory)*

### Functional Requirements

**Readings**

- **FR-001**: The Hall reading MUST be the median of five raw 12-bit ADC samples of GPIO39 per
  control step, passed to the motion logic as counts (no scaling).
- **FR-002**: The supply voltage MUST be read from GPIO36 in calibrated millivolts (the average of
  four samples) and scaled by the 100k/22k divider (× 122 / 22).
- **FR-003**: With current sense on, the motor current MUST be the larger of the two sense inputs
  (GPIO34, GPIO35) in calibrated millivolts, scaled by the 10k/20k divider (× 30 / 20) and by the
  amps-per-volt setting. With current sense off it MUST be passed as "no reading" (NaN), and the
  current inputs are not read.
- **FR-004**: The park switches (GPIO32, GPIO33) and the rain contact (GPIO4) MUST be read with the
  internal pull-ups on. A level counts once read three times in a row; before that, and after one
  second without three equal readings in a row, the input reads unreadable. Low means a closed
  contact: park switch closed (parked), rain contact closed (raining).

**Control task**

- **FR-005**: The motion logic MUST be stepped by a dedicated task every 20 ms, at a priority above
  the main loop and the web server, and it MUST NOT wait on the network.
- **FR-006**: After every step, command, fault clear, raised fault and settings hand-over, the
  logic's output MUST be applied to the driver through `lib/DriverLogic` and `MotorDriver`, which
  stays the only writer of GPIO25, 26 and 27. Steps and commands MUST be serialised.
- **FR-007**: A watchdog independent of the control task MUST check, every 10 ms, the time since
  the last step; if the driver is on and the gap exceeds 100 ms it MUST switch EN off at once and
  block the driver from being switched on until the control task has latched a watchdog fault
  (stating the gap and the limit).
- **FR-008**: The driver's PWM MUST run at 20 kHz with 8-bit duty. Switching on MUST set the other
  direction's PWM to 0 before its own and EN last; switching off MUST drop EN first.
- **FR-009**: The control task MUST also be watched by the system task watchdog, so a hung task
  restarts the device (and the EN pull-down holds the driver off).

**Boot**

- **FR-010**: At start-up the motion logic MUST be created with the saved settings, any saved fault
  latched, and only then MUST the control task start. The state follows the Hall reading: unknown
  between the magnets, and no motion until a command.

**Fault persistence**

- **FR-011**: The latched fault (kind, measured value, limit, and for a fault raised by this layer
  its cause: watchdog, park interlock or unreadable record) MUST be saved in NVS when it changes and
  removed when it clears.
- **FR-012**: At start-up a saved fault MUST be latched before the first step. A record that can't
  be decoded MUST latch a fault ("stored fault unreadable"), never be ignored.

**Status**

- **FR-013**: `/api/status` and `/ws/status` MUST carry a `roof` section: state, positionKnown,
  positionMm, positionPercent, target (or null), driving, fromUnknown, moveElapsedMs, moveTimeoutMs,
  hall {level, counts}, supplyVolts (or null), supplyLow, minSupplyVolts, currentSense, motorAmps
  (or null), maxCurrentAmps, parkSwitches {fitted, switch1, switch2}, rainContact, fault {kind,
  cause, measured, limit, text} (or null when clear), watchdogTrips, maxStepGapMs.
- **FR-014**: A change of the roof's state or fault MUST trigger a live update within 250 ms.

**Commands**

- **FR-015**: `POST /api/roof/open`, `/api/roof/close`, `/api/roof/stop` and
  `/api/roof/clear-fault` MUST call `requireAuth`. An accepted command answers 200 with the new
  state; a refused one answers 409 with the reason code and the text stating the measured value and
  the limit or the failed input. Commands from the REST API count as a person's.
- **FR-016**: A settings save (the settings page or a restore) that changes any `roof.*` or `park.*`
  setting MUST be refused with 409 while the roof is opening or closing. Accepted settings MUST be
  handed to the motion logic, which takes them once no move is active.

**Safety gates (constitution II and III)**

- **FR-017**: A close from any origin MUST be refused unless the park result is "parked" at that
  moment and every fitted park switch reads closed in the control task.
- **FR-018**: While the roof is closing, each step MUST check the same park condition; if it no
  longer holds, a fault (cause: park interlock) MUST be latched on that step.
- **FR-019**: An open from any origin MUST be refused unless the weather verdict is "safe". While
  the roof is opening, a verdict that is not safe MUST stop the move (stop, not a fault).
- **FR-019a**: If the weather verdict and park result haven't been handed to the roof for more
  than 5 s (a stalled main loop), the gates MUST treat the weather as not safe and the park as not
  confirmed, saying so.
- **FR-020**: Stop MUST always be accepted. Every roof command route MUST require authentication
  (constitution VIII).

**Settings**

- **FR-021**: The settings model MUST gain `roof.currentAmpsPerVolt` (default 8.5, range 0.5-100
  A/V), with the same range rule and message on the device and in the web UI. Settings saved before
  this spec MUST load with the default.

**Wording**

- **FR-022**: Every fault and refusal text MUST state the measured value and the limit, or the input
  that failed (constitution III), e.g. `Supply 10.9 V < 11.0 V`, `Mount not parked: park switch 1
  open`, `Control step 140 ms late > 100 ms`.

**Added after the PR review**

- **FR-024**: While the roof is opening or closing, settings saves, restores, firmware and web UI
  uploads, WiFi changes and restarts MUST be refused (409); a scheduled restart, the alerts pause
  state's NVS write and MQTT reconnects MUST wait until it stops.
- **FR-025**: The web server MUST run below the control task's priority.
- **FR-026**: With current sense on, the reading MUST be scaled back by the PWM duty applied while it
  was read. With the `roof.minRunningAmps` setting above 0, a reading below it after a second of
  driving MUST count as unreadable. It is off by default: the ADC can't tell the lowest currents from
  0 V, so detection of a broken sense wire is only what the owner's bench measurement supports.
- **FR-027**: Roof commands MUST carry the header `X-RoofRoof: 1` (403 without it), and the web UI
  MUST send it.
- **FR-028**: EN MUST only go high while the watchdog's last check is at most 50 ms old (research
  R15); the oldest heartbeat a step has seen is shown in the status (`maxWatchdogBeatAgeMs`). When the
  logic wants the driver on and the check is older, the roof MUST latch a fault (`watchdogStopped`)
  rather than hold EN low silently.
- **FR-029**: A close always wins over a flash write. An upload in progress counts as busy only while
  it makes progress (no chunk for 5 s, or a dropped connection, ends it). Open is refused while an
  upload or command-line OTA writes the flash, an MQTT connect is under way or a restart is
  scheduled. A close (any origin) cancels a scheduled restart, stops an upload at its next chunk
  (the upload fails with 409) and, if an upload or MQTT connect is still finishing, is accepted as
  waiting (202, `roof.closePending`) and starts as soon as it is done. These side effects and the
  wait happen only for a close that would run: past the park gate and the motion logic's own checks,
  and never when the roof is already closed or closing. Stop, and an open that runs, cancel a
  waiting close; a refused open doesn't. A fault ends a waiting close (clearing the fault never
  runs it). A restart after a filesystem upload (finished or stopped part-way) is never cancelled:
  it waits while the roof moves or a close waits. No MQTT connect is tried while the roof moves or a
  close waits; the check is repeated right before every connect.
- **FR-030**: Command-line OTA MUST only be serviced while the roof is closed (it blocks the main loop
  for the whole upload and can't be stopped part-way), the roof state MUST be re-checked when the
  upload starts (refused before the first write if it isn't closed), and it counts as a flash write
  while it runs.
- **FR-031**: Each upload request MUST keep its own state (its flash activity and why it stopped), so
  one request's refusal or disconnect never affects another's.

**Docs**

- **FR-023**: The wiring page MUST describe what the firmware now reads on every pin, the polarity
  of the switch inputs and the PWM frequency; a new calibration page MUST give the Hall, supply and
  current calibration procedure; the REST and WebSocket references MUST document the new routes and
  the `roof` section.

### Key Entities

- **Hardware readings**: Hall counts, supply volts (or none), motor amps (or none), park switch 1
  and 2 and rain contact (closed, open/dry, unreadable).
- **Fault record**: the latched fault (kind, measured, limit) and its cause, as saved in NVS.
- **Gate inputs**: the weather verdict and its reasons, the park result and its reasons.
- **Roof status section**: what FR-013 lists.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every pure rule in this spec (scaling, median, input debouncing and chatter, the
  watchdog limit, the fault record, the gates and interlocks, the settings-save guard, the status
  section, the wording) has native tests, including each boundary (one below, at and one above the
  limit).
- **SC-002**: On the bench, a stalled control task switches EN off within 110 ms of its last step
  (bench check).
- **SC-003**: On the bench, a latched fault survives 5 power cycles and clears only on a person's
  command (bench check).
- **SC-004**: A roof command, from the moment it is accepted to the driver output changing, takes no
  longer than one control step plus the dead time of spec 002 for a start, and is immediate for a
  stop (bench check with an analyser).
- **SC-005**: The full local check (firmware build, native tests, web tests, Playwright, quality
  gate, docs build, flash budget below 75% of the OTA slot) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **This spec checks the park before every close and throughout it** (constitution II: no path
   skips it), and **refuses an open unless the weather is safe** (III). Spec 005 adds the automatic
   sequence on top of the same checks. A new device with no N.I.N.A. or no SQMeter configured
   therefore refuses close or open respectively; commissioning sets those up first.
2. **An open stops (not faults) when the weather stops being safe.** Stopping keeps the estimate,
   so spec 005 can close from there. It is not a fault: nothing went wrong with the roof.
3. **Losing the park confirmation during a close is a latched fault**, as the constitution says,
   with the cause "park interlock" and the park result's reasons in the text.
4. **The park switch and rain contact polarity is fixed** (closed to ground = low = parked or
   raining). An inverted park input would read a broken wire as parked, so it is not offered. A
   broken rain contact wire reads dry; the rain contact is an extra source next to SQMeter, which
   is the weather source.
5. **Switch and contact inputs need three equal readings in a row (40 ms at 20 ms per step)** and
   read unreadable after one second of chatter, which is "not parked" and "unsafe".
6. **REST commands count as a person's** (authenticated, constitution I's "explicit,
   authenticated command from a person"). Spec 005's automatic close uses the automatic origin.
7. **Stop needs authentication too** (constitution VIII lists every endpoint that changes state).
   Stop is still always accepted once authenticated.
8. **The current-sense scale is a setting** (`roof.currentAmpsPerVolt`, default 8.5 A/V), since the
   sense ratio and the IBT-2's sense resistor vary; the divider ratios are fixed by the pin table.
9. **Settings saves that change `roof.*` or `park.*` are refused while opening or closing**, from the
   settings page and from a restore alike. Other settings save normally. OTA, restarts and the
   other platform actions while moving are spec 010.
10. **The watchdog limit equals the motion logic's step-gap limit (100 ms)**, checked every 10 ms by
    a high-priority timer, and it only acts while the driver is on. A late task with the driver off
    latches nothing.
11. **The control rate is 20 ms**, the rate spec 002's Hall confirmation and step-gap limit assume.
12. **A damaged stored fault record latches a fault** rather than being ignored.
13. **The PWM frequency is 20 kHz** (inaudible, within the IBT-2's rating) with 8-bit duty, matching
    the logic's 0-255 duty.
14. **If the control task or the watchdog can't start, the motor can never be switched on** until a
    restart: commands and clear-fault answer "not started", and the driver isn't armed.
15. **A watchdog trip survives a crash restart.** The trip leaves a marker in memory that survives a
    reset (not a power cut); if the device restarts before the trip's fault reached NVS (the task
    hung and the system watchdog restarted it), the fault is latched at start-up.
16. **Known limits, accepted for now:**
    - an NVS partition that ESP-IDF erases and reformats at boot (no free pages, or a newer NVS
      format) loses a stored fault, as it loses the settings;
    - the watchdog (an esp_timer) stalls with the control task while the flash cache is off. This
      spec refuses or defers every flash write while the roof moves (FR-024), but one that slips
      through (a race between a save and spec 005's automatic close) leaves EN as it was for the
      write's duration (about 45 ms, worst case a few hundred). A hardware timeout on EN is the
      later fix (research R12);
    - the park result may be up to about 6 s old at the gate during a close (5 s hand-over limit
      plus SafetyInputs' 1 s evaluation), while the fitted park switches are checked every 20 ms;
    - the park rule uses only the latest N.I.N.A. poll (the owner's decision in spec 004), so one
      failed poll during a close stops it with `parkInterlock`;
    - an intermittent park switch wire that settles closed reads closed until it has chattered for
      a second (three equal readings confirm a level);
    - the watchdog-alive check and EN gating read the clock separately, so in the step that first
      finds the watchdog stale, EN can be held low for that step before the fault is latched on the
      same call (no step passes silently);
    - a broken current-sense wire is only caught when `roof.minRunningAmps` is set from a bench
      measurement; an optional pull-up at the divider's input makes a broken line read high (an
      over-current fault) instead (wiring page);
    - a waiting close waits at most for an upload to notice the stop (its next chunk, or 5 s without
      one) or for an MQTT connect to finish (about 3 s plus DNS);
    - spec 010 builds on FR-024 (OTA, restarts and WiFi under motion).
16a. **AsyncTCP runs at priority 3** (`CONFIG_ASYNC_TCP_PRIORITY`), below the roof's 5; its default
    of 10 would outrank the control step.
16b. **Roof commands need `X-RoofRoof: 1`** against cross-site requests.
16c. **Current sense under PWM** is scaled by the duty. The broken-wire floor is the setting
    `roof.minRunningAmps`, off (0) by default. A capacitor across each sense divider's lower resistor
    is recommended.
16d. **A close always wins over an upload**; command-line OTA only runs while the roof is closed
    (it can't be stopped part-way safely).
17. **The roof status section is emulated by the demo as a closed roof** with fixed readings; the
    simulated roof is spec 011.

## Out of scope

- The automatic close on unsafe weather and its sequence (005), Alpaca (006), alert events (007),
  MQTT and Home Assistant (008), the dashboard card and settings page (009), OTA, restart and Wi-Fi
  behaviour while moving (010), the demo's simulated roof and bench mode (011), the commissioning
  guide (012).
- An abort route: Alpaca's AbortSlew (006) is its first user.
