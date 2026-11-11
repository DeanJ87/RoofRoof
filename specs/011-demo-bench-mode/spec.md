# Feature Specification: Demo and Bench Mode

**Feature Branch**: `spec/011-demo-bench`

**Created**: 2026-11-11

**Status**: Draft

## Context

Two ways to see RoofRoof's roof without a roof:

- **The public demo** runs the firmware's own decision code in the browser (SQMeter spec 016,
  RoofRoof spec 001). Until now it had no roof: the status showed a fixed closed roof, the Alpaca
  device list was empty, and nothing could be opened, closed or made to go wrong. This spec gives
  the demo a simulated roof: the real motion logic (spec 002), the real command checks (003, 006),
  the real close-on-unsafe sequence (005) and the real roof alerts (007), driving a simulated
  roof (motor, magnets, Hall sensor, motor current and the 12 V supply) instead of hardware.
  The Demo panel can make things go wrong on purpose: unsafe weather, rain, a mount that won't
  park, a jam and a low 12 V supply.
- **Bench mode** is a separate firmware build for a bare ESP32 on the desk. The same simulated
  roof replaces the motor and sensors, so the web UI, the REST and MQTT commands and the Alpaca
  Dome (including ConformU) can be tried on the real board. The motor driver is never switched on.
  Constitution I and IV ask for exactly this ("verified on the bench (bench mode, no motor)";
  "ConformU ... runs against real hardware only in bench mode").

Bench mode is safety-critical in a way the demo isn't. A bench board reports a roof that isn't
there. If bench firmware ever ran on the real roof, it would tell N.I.N.A. the roof had closed
while the real roof stood open in the rain. So bench mode must be impossible to reach by
accident on a real roof, must never drive the real motor, and must say what it is everywhere a
person or a client looks.

### Already in place before this spec

| Piece | From | Where |
|---|---|---|
| The motion logic, pure and native-tested | 002 | `lib/RoofLogic` |
| A simulated roof for the native tests (motor, magnets, Hall, current, supply, faults) | 002 | `test/roof_sim/RoofSim.h` |
| The command checks shared by the firmware and the Alpaca simulator | 003, 006 | `lib/RoofIoLogic` (`checkCommand`, `DomeBridge`, gates) |
| The close sequence and its alerts | 005, 007 | `lib/CloseSequenceLogic` |
| The roof section of the status document | 003 | `RoofIo::writeStatus` |
| The firmware marker in every image, checked before a web upload is installed | 001 | `lib/FirmwareImage`, `build=standard` |
| The device core in WebAssembly, the Demo panel and the tour | 001 | `tools/demo-core`, `web/src/demo` |

### Waiting on specs 008 and 009

Spec 008 adds the close phase and the weather and park checks to the roof status, and the MQTT
roof document. Spec 009 adds the dashboard roof card. This spec's demo wiring for the card and
the tour steps that point at it are done once both are merged; everything else here doesn't
depend on them.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under "Assumptions
decided for the owner".

- Q: How is bench mode switched on: a setting, a jumper, or a separate firmware build? → A: A
  separate firmware build only. The standard firmware has no bench code at all, so no setting,
  command, restore or MQTT message can switch a roof controller into bench mode. A bench image
  is marked as such; the standard firmware refuses to install it over the web upload and over
  command-line OTA, so it can only reach a board over USB.
- Q: What if a bench image ends up on the real roof anyway (flashed over USB onto the roof's
  board)? → A: The bench firmware watches the real 12 V supply input. A bare board has no 12 V
  supply; the roof has. While the real input reads 6 V or more (or can't be read) for a second,
  the bench firmware latches a motion fault ("bench firmware on a powered roof"), so the roof
  reads Error, the Dome reports a shutter error, and nothing moves, simulated or real. The fault
  is kept like any other and needs a person to clear it; it comes straight back while the supply
  is still there.
- Q: Should bench mode publish releases? → A: No. The bench image is built by CI to prove it
  compiles, never published as a release asset; it is built locally with its own build
  environment.
- Q: Where does the bench roof start after a restart? → A: Half-way, so it reads unknown and the
  Dome reports a shutter error until a person moves it (slowly, to the first magnet). Safest: a
  bench board can never start by reporting a closed roof. The demo is different: it keeps the simulated roof's
  position in the tab, so a page reload works like the device's restart (unknown between the
  magnets).
- Q: Can bench mode inject faults like the demo? → A: Not in this spec. Bench mode exists to try
  the UI, the commands and Alpaca on a bare board; the fault paths are covered by the native
  tests and can be tried in the demo. Injection on a board would need a new authenticated
  endpoint only one build has.
- Q: Should alerts from a bench board say so? → A: Yes. Every alert a bench board sends has
  "Bench: " in front of its title, so a bench board that was given the roof's alert settings
  (a restored backup) can't be mistaken for the roof.
- Q: How fast is the demo roof? → A: The device's default settings (1850 mm at 38 mm/s, about
  50 s end to end). The Demo panel's 10x clock makes it about 5 s. The demo honours the roof
  settings the visitor saves, as the device would.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The demo has a roof that behaves like the real one (Priority: P1)

A visitor opens the public demo and finds a roof: it's closed, the weather is safe and the mount
is parked. They open it and watch the position climb until the open magnet stops it. They close
it. Through the Alpaca Dome, an imaging app's view of the same roof agrees.

**Why this priority**: The roof is what the device is for. A demo without one shows settings and
alerts for a machine nobody can see work.

**Independent Test**: In the demo, open and close the roof from the REST commands and from the
Alpaca Dome; the status document, the Dome's ShutterStatus and the position agree at every step,
and the move takes the time the travel and speed settings say.

**Acceptance Scenarios**:

1. **Given** the demo just loaded, **When** the visitor looks at the status, **Then** the roof
   reads closed, position 0, the Hall reading at the closed magnet, the supply about 12.6 V.
2. **Given** a closed roof and safe weather, **When** the visitor opens it, **Then** the state
   goes opening, the position estimate rises, the roof slows near the end and stops on the open
   magnet, reading open, in about the travel time the settings give.
3. **Given** an open roof and the mount parked, **When** the visitor closes it, **Then** it closes
   the same way and reads closed.
4. **Given** the demo, **When** an Alpaca client lists the configured devices, **Then** the Dome
   is listed, and its ShutterStatus, Slewing, OpenShutter, CloseShutter and AbortSlew behave as on
   the device.
5. **Given** the roof is moving, **When** the visitor presses Stop, **Then** it stops part-way and
   reads stopped with its estimated position.
6. **Given** the visitor reloads the page while the roof is part-way, **Then** the device "restarts"
   and the roof reads unknown, as a real board would after a restart between the magnets.

---

### User Story 2 - The demo shows what happens when things go wrong (Priority: P1)

From the Demo panel, a visitor makes the weather unsafe, starts rain, makes the mount refuse to
park, jams the roof, or drops the 12 V supply. The device reacts the way the firmware does: the
automatic close parks the mount and closes the roof; a park that never comes leaves the roof open
with a critical alert; a jam stops the roof with a fault that needs clearing; a low supply refuses
moves and stops one under way.

**Why this priority**: The safety behaviour (constitution I-III) is the device's main value, and
a visitor can't see it without making it happen.

**Independent Test**: Trigger each injector from an open roof and check the state, fault, close
phase, alerts and Dome answers against what the native tests of the same logic expect.

**Acceptance Scenarios**:

1. **Given** an open roof and a parked-able mount, **When** the visitor makes the weather unsafe,
   **Then** the device asks the mount to park, waits for the park to hold, closes the roof, and
   the roof reads closed; a "closed after unsafe" alert appears.
2. **Given** an open roof, **When** the visitor starts rain, **Then** the same happens, with rain
   as the reason.
3. **Given** an open roof and "park fails" on, **When** the weather turns unsafe, **Then** the
   roof stays open, and once the park time limit runs out a critical "mount didn't park" alert
   appears (constitution II: never close onto an unparked mount).
4. **Given** a moving roof, **When** the visitor jams it, **Then** the roof stops with a fault
   (no magnet before the timeout, or over-current when current sense is on), motion is locked
   out, and Clear fault (a person) unlocks it once the jam is removed.
5. **Given** the roof, **When** the visitor drops the 12 V supply below its minimum, **Then** a
   move is refused with the measured value and the limit, and a move under way stops with a
   supply-low fault.
6. **Given** unsafe weather, **When** the visitor tries to open the roof (web or Alpaca), **Then**
   it's refused with the reason.
7. **Given** the mount not parked, **When** the visitor tries to close the roof, **Then** it's
   refused, saying the mount isn't parked.

---

### User Story 3 - Bench mode on a bare board (Priority: P1)

The owner flashes the bench build onto a spare ESP32 with nothing connected. The web UI shows a
roof that opens and closes, with a banner on every page saying it's bench mode. N.I.N.A. and
ConformU connect to the Dome and drive the simulated shutter. The motor driver's enable line
stays low the whole time.

**Why this priority**: The constitution requires motion and safety changes to be verified on the
bench before the roof, and ConformU against the real device only in bench mode. Without it, the
first real test of the firmware is on the roof.

**Independent Test**: Build the bench image; on a bare board, open, close and stop the roof from
the web UI and Alpaca; measure GPIO27 (EN), 25 and 26 (PWM): all stay low throughout.

**Acceptance Scenarios**:

1. **Given** a bare board running the bench image, **When** the owner opens and closes the roof,
   **Then** the simulated roof moves at the configured speed and stops on its simulated magnets,
   and EN, RPWM and LPWM never leave low.
2. **Given** the bench image, **When** any page of the web UI is open, **Then** a banner says
   bench mode: the roof is simulated and the motor is never driven.
3. **Given** the bench image, **When** a client reads the status document, the Alpaca Dome's
   description, or an alert, **Then** each says bench mode (the status's roof section, the Dome's
   description, "Bench: " on alert titles).
4. **Given** the bench image, **When** a motion fault would occur (the watchdog, the timeout), **Then**
   the same fault logic runs as on the roof.

---

### User Story 4 - Bench mode can't reach the real roof by accident (Priority: P1)

The roof's board runs the standard firmware. Someone uploads the bench image to it by mistake,
over the web UI or command-line OTA. It's refused. If it's flashed over USB onto the roof's board
anyway, the bench firmware sees the 12 V supply and refuses to simulate anything.

**Why this priority**: A bench image on the roof would report a closed roof that isn't. That
defeats constitution II and III without anyone noticing.

**Independent Test**: Upload the bench image to a standard board over the web UI and over
command-line OTA: both refuse it, and the board keeps running the standard firmware. Run the bench
image on a board with 12 V on the supply input: the roof reads Error with the bench-supply fault.

**Acceptance Scenarios**:

1. **Given** a board running the standard firmware, **When** a bench image is uploaded through the
   web UI, **Then** it's refused before anything is installed, with a message saying it's a bench
   image.
2. **Given** a board running the standard firmware with command-line OTA on, **When** a bench image
   is sent over command-line OTA, **Then** the board doesn't boot it: it stays on the standard
   firmware and logs why.
3. **Given** the bench image on a board whose 12 V input reads 6 V or more (or can't be read) for
   a second, **Then** the roof latches the bench-supply fault: state Error, Dome shutter error,
   nothing moves; clearing the fault while the supply is still there latches it again.
4. **Given** the standard firmware, **Then** there is no setting, command or document that turns
   bench mode on.
5. **Given** a board running the bench image, **When** the standard image is uploaded, **Then** it's
   accepted (leaving bench mode is always allowed).

---

### User Story 5 - The tour shows the roof (Priority: P2)

The demo tour points at the roof card, opens the roof, makes the weather unsafe, and lets the
visitor watch the automatic close.

**Why this priority**: The tour is how most visitors meet the device. It currently stops at the
status card and the alert bell.

**Independent Test**: Take the tour with "Do it for me" at every step; each action step finishes
when the device reacts, and the roof ends closed after the unsafe step.

**Acceptance Scenarios**:

1. **Given** the tour, **When** the visitor reaches the roof steps, **Then** the roof card is
   outlined and "Do it for me" opens the roof, then makes the weather unsafe, each step finishing
   when the device reacts, never on a timer.

---

### User Story 6 - The demo can't drift from the device (Priority: P2)

The demo's roof documents come from the firmware's own writers and are checked against the
device's contract schemas; a field that differs fails the build.

**Independent Test**: Change a roof field in the device's documented status shape without
updating the demo; the contract tests fail.

**Acceptance Scenarios**:

1. **Given** the demo, **When** the contract tests run, **Then** the status (with the roof moving,
   faulted and closed), the roof command replies and the Alpaca management documents all match
   the device's schemas.

### Edge Cases

- The demo's clock at 10x: the roof steps in 20 ms of device time however far the clock jumps,
  so the motion logic sees the same step rate as on the board.
- The visitor changes the roof settings in the demo (travel, speed, Hall thresholds or polarity,
  current sense): the simulated roof follows them at once, and the motion logic takes them when no
  move is active, as on the device.
- The visitor turns the SQMeter source off in the demo's settings: with no weather source the
  verdict is unsafe, so the automatic close runs, as on the device.
- The visitor clears N.I.N.A.'s address: the park can't be confirmed, so no close is possible, and
  an unsafe verdict ends in "mount didn't park".
- A jam injected while the roof is closed or open does nothing until a move starts; removing it
  doesn't clear the fault (only a person's Clear fault does).
- Rain and unsafe weather together: rain is named first; removing one leaves the other.
- The bench board's supply input floats (no divider fitted): it may read high, and the
  bench-supply fault latches. The fix is to tie the input to ground; the docs say so.
- A restart of the bench board mid-move: the simulated roof starts half-way (unknown) again; the real fault
  store still keeps any latched fault.
- The demo's simulated N.I.N.A. is asked to park while "park fails" is on: it answers that it's
  parking and never gets there.
- An upload or restart in the demo while the roof moves: the demo simulates uploads and
  restarts and doesn't model spec 010's holds (the native tests cover them).

## Requirements *(mandatory)*

### Functional Requirements

#### The simulated roof (shared)

- **FR-001**: One simulated roof model serves the native tests, the Alpaca simulator, the demo
  and bench mode. It moves the roof at the configured speed scaled by the duty, holds it at the
  end stops, reads the Hall sensor at each magnet from the configured thresholds and polarity
  (beyond each threshold, never at a rail), reports motor current (stall current at an end stop
  or a jam) when current sense is on, and a 12 V supply that sags a little under load.
- **FR-002**: The model can be told to: jam (the roof doesn't move, the motor stalls), drop the
  supply to a given voltage, and (for tests) wire the motor backwards, lose a magnet or put the
  Hall sensor at a rail.
- **FR-003**: The model only follows the driver command the motion logic decided (and, on the
  bench, what the motor driver layer would have applied); it never decides anything itself.

#### The demo's roof

- **FR-010**: The demo device runs the firmware's motion logic, command checks, gates, close
  sequence and roof alerts against the simulated roof, stepped every 20 ms of device time.
- **FR-011**: The demo answers `POST /api/roof/open|close|stop|clear-fault` as the device does
  (same status codes, fields and refusal texts), as a person's commands.
- **FR-012**: The demo lists the Alpaca Dome and answers it through the firmware's Alpaca router
  and Dome mapping, as an Alpaca client's commands (constitution IV; the same rules as spec 006).
- **FR-013**: The demo's status document carries the roof section written by the firmware's own
  writer, with `simulation: "demo"`.
- **FR-014**: The demo simulates SQMeter (a source answering safe, unsafe or rain, on the
  configured sources only) and N.I.N.A. (a mount that parks when asked, after a short slew, unless
  "park fails" is on). With a source or N.I.N.A. not configured in the settings, the demo behaves
  as the device does without them. Park switches, when the settings say they're fitted, follow
  the simulated mount (closed when it's parked); the rain contact, when fitted, follows the rain
  control.
- **FR-015**: The Demo panel has a Roof section with: weather safe / unsafe / rain; the mount
  parked or unparked (the imaging app's own park and unpark); park fails on or off; jam on or
  off; 12 V normal or low. Each control says what it does and shows its current setting.
- **FR-016**: Critical roof events raised in the demo appear in the demo's alert list like the
  device's (nothing is sent anywhere unless the visitor turned on real notifications).
- **FR-017**: The simulated roof's position, a latched fault and the panel's settings survive a
  page reload in the same tab; the reload is a restart (position unknown between the magnets).
  Reset demo starts from closed and safe.
- **FR-018**: The demo's dashboard shows the roof through spec 009's roof card; the tour has roof
  steps (open, then unsafe weather and the automatic close).

#### Bench mode

- **FR-020**: Bench mode exists only in a separate firmware build. The standard build contains no
  bench code; no setting, command, restore, MQTT message or Alpaca call can turn it on.
- **FR-021**: In the bench build, the motor driver's pins are set low at boot and never written
  high: EN (GPIO27), RPWM (GPIO25) and LPWM (GPIO26) stay low, and PWM is never attached. The
  decision is one pure function with a native test over every command and duty.
- **FR-022**: In the bench build, the simulated roof replaces the Hall, supply and current
  readings. Everything else is the standard firmware's: the control task, the watchdog (against
  the driver state the logic asked for), the gates, the close sequence, the fault store, REST,
  WebSocket, MQTT, Home Assistant and Alpaca.
- **FR-023**: The bench build watches the real 12 V supply input. A reading of 6.0 V or more, or
  an unreadable one, lasting 1 s latches an External fault with the cause `benchSupply`
  (measured: volts or NaN, limit 6.0 V). While the condition holds, the fault is raised again at
  once after a clear.
- **FR-024**: Bench images carry `build=bench` in their firmware marker. The standard firmware
  refuses a bench image over the web upload, with its own message. Over command-line OTA, the
  standard firmware checks the marker of the image it just received before booting it, and keeps
  the running firmware if the image isn't one it would accept over the web upload.
- **FR-025**: A bench board accepts both standard and bench images (leaving bench mode is always
  allowed), subject to the usual product and layout checks.
- **FR-026**: Bench mode is obvious:
  - the status document's roof section has `simulation: "bench"` (`"off"` on the standard build);
  - every page of the web UI shows a bench-mode banner while it does;
  - the Alpaca Dome's description starts "BENCH MODE (simulated roof, no motor)";
  - every alert's title starts "Bench: ";
  - the log says bench mode at start-up.
- **FR-027**: The bench image is built in CI to prove it compiles; it isn't a release asset.

#### Docs and checks

- **FR-030**: A user guide page explains bench mode: what it is for, how to build and flash it,
  the supply-input tie-down, what is and isn't simulated, and the safeguards. The demo page lists
  the Roof controls. The wiring/commissioning pages link to it.
- **FR-031**: The demo's contract tests cover the roof section in several states, the roof
  command replies and the Alpaca management documents with the Dome listed.

### Key Entities

- **Simulated roof (plant)**: position (mm, 0 closed), travel, speed at full duty, magnet zone,
  Hall counts at each magnet and between, supply volts and sag, running and stall current, and
  the injected faults.
- **Bench supply guard**: whether a real supply has been seen on a bench board (latched for the
  boot).
- **Demo roof**: the controller, the simulated roof, the simulated SQMeter and N.I.N.A., the close
  sequence and the panel's injector settings.
- **Firmware build kind**: `standard` or `bench`, in the image marker and the running firmware.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In the demo, an open and a close each take the travel time the settings give, within
  10%, and end on the target magnet.
- **SC-002**: Every injector (unsafe, rain, park fails, jam, 12 V low) gives the same state, fault
  and alert in the demo as the native tests of the same logic expect; each has a test.
- **SC-003**: The bench build never writes EN, RPWM or LPWM high: a native test covers every
  command and duty, and the bench check measures the pins.
- **SC-004**: The standard firmware refuses a bench image over both upload paths (native tests of
  the decision, and a bench check on a board).
- **SC-005**: A bench image with 12 V on the supply input reads Error within 2 s of start-up and
  never reports a closed shutter.
- **SC-006**: The standard build's flash use grows by less than 4 KB; the bench build fits its slot.
- **SC-007**: The tour's roof steps finish on the device's reaction, with "Do it for me", in under
  a minute at the 10x clock.

## Assumptions decided for the owner

- **Bench mode is a separate build only**; there is no runtime switch. Safest: nothing on a
  running roof controller can turn it on.
- **The standard firmware refuses bench images** over the web upload and command-line OTA; USB is
  the only way to install one.
- **A bench board refuses to simulate while it sees a 12 V supply** (6.0 V or more, or
  unreadable, for 1 s) and latches a fault that a person must clear. A bare board whose supply
  input floats may trip it; tying the input to ground is the documented fix.
- **The bench roof starts half-way (unknown)** after every restart; the demo keeps its roof's position across a
  reload and treats the reload as a restart.
- **No fault injection on the bench board** in this spec; the demo and the native tests cover the
  fault paths.
- **Bench alerts are labelled** "Bench: " in the title.
- **The bench image is never a release asset**; CI only builds it.
- **The demo roof uses the device's default speed and travel** (about 50 s per move); the 10x
  clock shortens it.
- **The demo's defaults** configure an SQMeter HTTP source and a N.I.N.A. address, so the demo
  starts safe and parked; the simulated N.I.N.A. takes about 8 s to park.
- **The demo doesn't model spec 010's upload and restart holds**; uploads and restarts stay
  simulated as before.

## Out of scope

- Fault injection on a bench board.
- Measured travel and calibration (spec 012's commissioning guide).
