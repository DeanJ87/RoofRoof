# Feature Specification: Alpaca Dome

**Feature Branch**: `spec/006-alpaca-dome`

**Created**: 2026-11-11

**Status**: Draft

## Context

Spec 001 kept SQMeter's Alpaca server (discovery, the management API and the device router) but
offered no device, so N.I.N.A. could find the server and had nothing to connect to. Spec 003 runs
the roof on the board with checked commands (open only while the weather is safe, close only onto a
confirmed park), and spec 005 closes the roof by itself when the weather turns unsafe.

This spec adds the roof as an ASCOM Alpaca **Dome** with a shutter and nothing else: an imaging app
can open the roof, close it, stop it and read whether it is open, closed or moving. Every Alpaca
command goes through the same checks as the web UI's, so a client can never do what a person at
the web UI couldn't, and it can do less: it can't move a roof whose position is unknown or clear a
fault. ASCOM ConformU comes back in CI against a desktop simulator that runs the same Alpaca code
and the same roof logic.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Does an Alpaca client count as a person for moving the roof from an unknown position or for
  clearing a fault? → A: No. Alpaca has no login (constitution VIII: open by design), and
  constitution I needs an authenticated person for both. A client gets the same refusal as the
  automatic close ("Position unknown: only a person can move the roof"), and there is no Alpaca
  member that clears a fault.
- Q: Does an Alpaca `AbortSlew` count as a person's stop for the automatic close (spec 005)? → A:
  No. Alpaca has no login, so anything on the network can send it; if it counted, one request (a
  script, ConformU pointed at the device, an app's clean-up) could call off the close on unsafe
  weather, which constitution III forbids and VIII limits Alpaca to I-III.
- Q: Can a client stop a close while the weather isn't safe? → A: No, whoever started the close
  (spec 005's, a person's or the client's own). While a close is under way or waiting to start and
  the weather verdict isn't safe, a client's `AbortSlew` is refused (InvalidOperation, "The roof is
  closing and the weather isn't safe: an imaging app can't stop it (Stop in the web UI can)").
  Otherwise a client could keep the roof open in the rain, by stopping each attempt or by starting
  a close of its own and stopping that (the sequence would watch it, see it interrupted and wait 5
  minutes; third safety review). The rule depends only on the verdict and the roof's state, not on
  who started the close. The web UI's Stop (an authenticated person) always works and stands the
  sequence down (spec 005); the controller's own stops always work. Every other stop from a client
  works at once.
- Q: Must a client be connected before it can move the roof? → A: Yes for `OpenShutter` and
  `CloseShutter` (ASCOM's NotConnected error otherwise). This follows ASCOM and catches a client
  that forgot to connect; it is not a safeguard (the connection is one flag for the whole server,
  anything can set it, and it survives a self-caused restart). The safeguards are the roof's own
  checks. `AbortSlew` is accepted connected or not.
- Q: What does the Dome do while Alpaca support is switched off? → A: It isn't listed, and every
  Dome request, `AbortSlew` included, is answered with the "Alpaca support is disabled" error;
  nothing moves or stops. Switched off, Alpaca changes nothing (the web UI's Stop remains). The
  setting stays off by default, so an update never starts accepting roof commands that were off
  before.
- Q: Does a client disconnecting, or going silent, stop the roof? → A: No. A move has its own
  timeout and magnet checks in the motion code (constitution I: never enforced by a client), and
  stopping a close half-way would leave the roof neither open nor closed. The imaging-app alerts
  report a client that went silent.
- Q: What does `ShutterStatus` say for a roof stopped part-way, or with its position unknown? → A:
  `shutterError`, as for a latched fault. Only the closed magnet reads closed and only the open
  magnet reads open: an app waiting for "open" must not start slewing the mount under a part-open
  roof, and one waiting for "closed" must not stop waiting. An app that wants the roof closed can
  still call `CloseShutter`.
- Q: Is `AbortSlew` the roof's Stop or its Abort? → A: Its Stop: the motor goes off at once and the
  position estimate is kept. The roof's Abort makes the position unknown, and an unknown position
  stops the automatic close from closing by itself (spec 005 FR-010), so an Alpaca abort would call
  off the close on unsafe weather after all. Constitution I's "unknown after an abort" is about the
  roof's Abort command, which Alpaca doesn't send.
- Q: Can a client reverse a close? → A: No. A client's `OpenShutter` is refused while the roof is
  closing, a close is waiting to start, or a close was stopped part-way (whoever asked for the
  close); the client can close it again, or a person opens it from the web UI (spec 003). Without
  the last case, a client could stop a person's close and reopen the roof in two requests.
- Q: Does the demo get the Dome? → A: Not in this spec. The demo has no simulated roof until spec
  011; until then it offers no Alpaca device and says why.
- Q: What are the Dome's name and description? → A: Name "RoofRoof roof"; description "Roll-off
  roof: shutter open, close and stop only". Fixed text, not the device name setting, so a client's
  saved selection matches across renames (the UniqueID is what stays stable).
- Q: Is a client that repeats a command (N.I.N.A. retries, polling scripts) limited? → A: No separate
  limit. A repeated open or close towards the end the roof is already moving to, or already at, is
  accepted and changes nothing (spec 002), and every command takes the same short lock as the web
  UI's, so repeats can't start extra moves or hold the motion task up.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | The roof never moves by itself while its position is unknown; only an authenticated person may | FR-006: a client is not a person |
| I | Any motion fault locks out motion until a person clears it | FR-006: no Alpaca member clears a fault; open and close are refused while one is latched |
| I | A move's timeout is enforced by the motion code, never by a client | FR-008: a disconnect or silence never ends or extends a move |
| I, VII | A person's stop is never refused (the web UI's Stop on every page) | FR-007: only an Alpaca client's stop can be refused, and only to keep a close in bad weather going |
| III | Unsafe ends closed: nothing an unauthenticated client sends can call off or delay a close in bad weather | FR-007: a client can't stop a close while the weather isn't safe and its stops never stand the sequence down; FR-005: a client can't reverse a close |
| II | Every close is checked the same way whatever its source, including Alpaca `CloseShutter` | FR-005: the web UI's checked command path |
| III | Opening is refused while the verdict is not safe; the roof never opens by itself | FR-005 |
| III | Every refusal states the measured value and the limit, or the input that failed | FR-005: the Alpaca error carries the refusal's text |
| IV | The Dome implements OpenShutter, CloseShutter, AbortSlew, ShutterStatus and Slewing with CanSetShutter true; everything else is false or NotImplemented | FR-002 to FR-004 |
| IV | Open and close return at once and report progress through ShutterStatus; a refused move returns an error saying why | FR-005 |
| IV | ConformU passes with zero errors, issues and configuration alerts in CI against the simulator | FR-012 |
| IV | UniqueID and discovery stay stable | FR-001 |
| VIII | Alpaca is open by design but moves the roof only within Principles I-III | FR-005 to FR-007 |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - N.I.N.A. opens and closes the roof (Priority: P1)

The owner adds RoofRoof's Dome in N.I.N.A.'s equipment tab and connects. At dusk the sequence opens
the shutter; at the end of the night it parks the mount and closes the shutter. N.I.N.A. sees the
roof opening, open, closing and closed.

**Why this priority**: This is what the Alpaca device is for (constitution IV).

**Independent Test**: The simulator, driven over HTTP, opens the roof and reports opening then open,
closes it and reports closing then closed, and ConformU's Dome checks pass against it.

**Acceptance Scenarios**:

1. **Given** Alpaca on, **When** a client lists the configured devices, **Then** it sees one Dome,
   number 0, with a UniqueID made from the board's MAC that doesn't change across restarts or updates.
2. **Given** a connected client, the roof closed, the weather safe, **When** the client calls
   `OpenShutter`, **Then** the call returns at once without error, `ShutterStatus` reports opening and
   `Slewing` true, then open and `Slewing` false once the open magnet is read.
3. **Given** a connected client, the roof open, the mount parked, **When** the client calls
   `CloseShutter`, **Then** the call returns at once without error, `ShutterStatus` reports closing,
   then closed at the closed magnet.
4. **Given** the roof already open, **When** a client calls `OpenShutter`, **Then** it succeeds and
   nothing moves.

---

### User Story 2 - Alpaca obeys every safety rule (Priority: P1)

N.I.N.A. asks for something the safety rules don't allow: opening while it rains, closing onto a
mount that isn't parked, moving a roof whose position is unknown or that has a fault. The roof
doesn't move, and N.I.N.A. shows an error that says why.

**Why this priority**: A remote client can now command the roof (constitution I-III, VIII).

**Independent Test**: Native tests of the Alpaca router with a stub roof, and of the roof control
with the client origin, show each refusal returns an Alpaca error with the same text as the web UI's
and that nothing moved.

**Acceptance Scenarios**:

1. **Given** the weather verdict not safe, **When** a client calls `OpenShutter`, **Then** the roof
   doesn't move and the reply is an InvalidOperation error starting "Weather not safe:" with the
   verdict's reasons.
2. **Given** the mount not confirmed parked, **When** a client calls `CloseShutter`, **Then** the roof
   doesn't move and the error starts "Mount not parked:" naming the input that failed.
3. **Given** the roof's position unknown, **When** a client calls `OpenShutter` or `CloseShutter`,
   **Then** the roof doesn't move and the error says only a person can move it.
4. **Given** a latched fault, **When** a client calls `OpenShutter` or `CloseShutter`, **Then** the
   roof doesn't move and the error says a person must clear the fault, and names the fault with its
   value and limit.
5. **Given** a client that has not connected, **When** it calls `OpenShutter` or `CloseShutter`,
   **Then** nothing moves and the reply is a NotConnected error.
6. **Given** Alpaca switched off, **When** any Dome request arrives, **Then** nothing moves or stops
   and the reply says Alpaca support is disabled.
7. **Given** a close under way from a client, **When** the park is lost, **Then** spec 003's interlock
   stops the roof with a fault, exactly as for a close from the web UI.

---

### User Story 3 - Abort stops the roof (Priority: P1)

The owner sees something wrong on the camera and presses Stop in N.I.N.A.'s dome panel while the
roof is opening, or closing at their own request. The roof stops at once.

**Why this priority**: Stop must work wherever a person may press it (constitution I, VII), within
constitution III: a close while the weather isn't safe is stopped only from the web UI.

**Independent Test**: With the roof opening or closing at a person's or a client's request,
`AbortSlew` stops the motor before the reply and `ShutterStatus` then reports error (stopped
part-way). With a close under way and the weather not safe, a client's `AbortSlew` is refused and
the close runs on.
A client's stop never counts as a person's stop.

**Acceptance Scenarios**:

1. **Given** the roof opening, or closing in safe weather, **When** a client calls
   `AbortSlew`, **Then** the motor is off before the reply, the position estimate is kept, and
   `Slewing` is false.
2. **Given** a close running or waiting to start (spec 005's, a person's or a client's) and the
   weather not safe, **When** a client calls `AbortSlew`, **Then** the reply is an InvalidOperation
   error saying the web UI's Stop can stop it, and the close carries on.
2a. **Given** it is raining and a client keeps sending `CloseShutter` and `AbortSlew`, **When** any
   time passes, **Then** the roof still ends closed onto the parked mount.
3. **Given** a client that has not connected, **When** it calls `AbortSlew` during a move it may
   stop, **Then** the roof stops.
4. **Given** nothing moving, **When** a client calls `AbortSlew`, **Then** it succeeds and nothing
   changes.
5. **Given** the automatic close waiting for the park, **When** a client calls `AbortSlew`, **Then**
   it succeeds (nothing moves), and the sequence carries on waiting and closes once the park is
   confirmed.
6. **Given** Alpaca switched off, **When** a client calls `AbortSlew`, **Then** it gets the disabled
   error and nothing changes.

---

### User Story 4 - Conformance is checked on every change (Priority: P2)

A change to the Alpaca code or the roof logic opens a pull request. CI builds the simulator, which
runs the same Alpaca router and roof logic with a simulated roof, and runs ConformU's conformance
and Alpaca protocol checks against the Dome. Any error, issue or configuration alert fails the job.

**Why this priority**: Constitution IV; spec 001 suspended ConformU until a device existed.

**Independent Test**: The workflow runs on a pull request that touches `lib/AlpacaLogic` and reports
both checks passed; the same commands pass locally.

**Acceptance Scenarios**:

1. **Given** the simulator running, **When** ConformU's conformance check runs against the Dome,
   **Then** it finishes with zero errors, issues and configuration alerts, including its shutter
   open and close tests.
2. **Given** the simulator running, **When** ConformU's Alpaca protocol check runs, **Then** it
   finishes with zero errors and issues.
3. **Given** a simulator started with the weather unsafe, **When** a client calls `OpenShutter`,
   **Then** it gets the weather refusal (the simulator is a usable test target for refusals).

---

### User Story 5 - The imaging-app watch covers the Dome (Priority: P3)

The owner has the imaging-app alerts on. N.I.N.A. crashes mid-night. After the silence time the
owner gets "imaging app stopped checking" naming the roof, and the Alpaca page shows the Dome with
its live state.

**Why this priority**: SQMeter's client watch already exists; it only needed a device.

**Independent Test**: Native tests feed the client watch Dome requests and silence and see the
watching, silent and disconnected states for the Dome; web tests show the Dome on the Alpaca page
and the imaging-app tile on the dashboard.

**Acceptance Scenarios**:

1. **Given** a client polling the Dome, **When** it stops for longer than the silence time, **Then**
   the Dome is reported silent and the "stopped checking" alert names it.
2. **Given** Alpaca on, **When** the owner opens the Alpaca page, **Then** the Dome is listed with
   its UniqueID, API base and live `ShutterStatus` and `Slewing`.

---

### Edge Cases

- A close accepted but waiting for an upload or MQTT connect to stop (spec 003): the call succeeds,
  `ShutterStatus` reports closing and `Slewing` is true until it starts or is replaced.
- `CloseShutter` while opening: the same as the web UI: the move reverses through the dead time,
  subject to the same checks.
- An open under way when the weather turns unsafe: spec 003 stops it; the client then sees error
  (stopped part-way) and the close sequence takes over.
- `OpenShutter` while the roof is closing, a close waits, or a close was stopped part-way: refused
  ("The roof is closing or was stopped while closing: an imaging app can't reverse a close").
- The roof controller didn't start: `ShutterStatus` is error, open and close are refused saying so,
  `AbortSlew` succeeds (the motor is off already).
- The device restarts on its own while a client is connected: the connection is kept (SQMeter's
  connection memory), and the roof's own boot rules apply (position unknown away from a magnet).
- Parameters with the wrong casing in a PUT body, a missing `Slaved` value, a request for Dome 1:
  answered as the ASCOM Alpaca protocol says (ConformU's protocol check).
- `Slaved` set to false is accepted; set to true is NotImplemented.
- The web UI's own requests (tagged as the UI) don't count as an imaging app checking the Dome.

## Requirements *(mandatory)*

### Functional Requirements

**The device**

- **FR-001**: While Alpaca support is on, the device MUST offer exactly one Alpaca device: a Dome,
  number 0, with interface version 3 and a UniqueID built from the board's MAC (the existing
  `roofroof-<mac>-dome-0` form), unchanged across restarts and updates.
- **FR-002**: `CanSetShutter` MUST be true. `CanFindHome`, `CanPark`, `CanSetAltitude`,
  `CanSetAzimuth`, `CanSetPark`, `CanSlave` and `CanSyncAzimuth` MUST be false. `Altitude`,
  `Azimuth`, `AtHome` and `AtPark` MUST answer NotImplemented. `Slaved` MUST read false; setting it
  false MUST succeed and setting it true MUST answer NotImplemented. `FindHome`, `Park`, `SetPark`,
  `SlewToAltitude`, `SlewToAzimuth` and `SyncToAzimuth` MUST answer NotImplemented. Custom actions
  and commands stay unsupported.
- **FR-003**: `ShutterStatus` MUST report: closed at the closed magnet; open at the open magnet;
  opening while opening; closing while closing or while a
  close is waiting to start; error while stopped part-way, the position is unknown, a fault is
  latched or the roof controller isn't running. Only the open magnet reads open.
- **FR-004**: `Slewing` MUST be true while the roof is opening or closing or a close is waiting to
  start, and false otherwise.

**Commands**

- **FR-005**: `OpenShutter` and `CloseShutter` MUST go through the same checked command path as the
  web UI's open and close (weather gate, park gate and interlock, motion logic, uploads and restarts)
  and return at once. An accepted or deferred command MUST succeed. A refused one MUST answer the
  InvalidOperation error with the refusal's text; for a latched fault the text MUST also name the
  fault with its value and limit. Both MUST answer NotConnected while the client isn't connected,
  and the disabled error while Alpaca is off, without moving anything. A client's `OpenShutter`
  MUST be refused while the roof is closing, a close is waiting to start, or a close was stopped
  part-way. The decision MUST be one
  shared function that the firmware's command path and the simulator both call.
- **FR-006**: Alpaca commands MUST carry their own origin ("client"). The motion logic MUST treat it
  as not a person: it may not move the roof from an unknown position or clear a fault.
- **FR-007**: `AbortSlew` MUST stop the roof (the roof's Stop, keeping the position estimate)
  whether the client is connected or not, and MUST succeed when nothing is moving or the roof
  controller isn't running, except: while a close is under way or waiting to start and the weather
  verdict isn't safe (whoever started the close) it MUST be refused with InvalidOperation saying the
  web UI's Stop can stop it,
  and while Alpaca is off it MUST answer the disabled error. It MUST NOT count as a person's stop.
- **FR-008**: Disconnecting, a client going silent or Alpaca being switched off MUST NOT stop, start
  or extend a move.

**Device state and logging**

- **FR-009**: `DeviceState` MUST list `ShutterStatus`, `Slewing` and `TimeStamp` (the latter only when
  the clock is set), with the same values as the individual members.
- **FR-010**: Every Alpaca open, close and abort MUST be logged with the command, the client's ID when
  it sent one, and whether it was accepted, deferred or refused (with the refusal's text).

**Conformance**

- **FR-011**: The desktop simulator MUST serve the Dome through the same Alpaca router and the same
  mapping between the Dome and the roof as the firmware, driving the real motion logic against a
  simulated roof (motor, magnets, Hall sensor), with the weather and park inputs it is started with
  (safe and parked by default).
- **FR-012**: CI MUST run ConformU's conformance and Alpaca protocol checks against the simulator's
  Dome on every change to the Alpaca code, the roof logic or the simulator, and fail on any error,
  issue or configuration alert. Spec 001's Complexity Tracking exception for ConformU MUST be marked
  resolved.

**Imaging app and web UI**

- **FR-013**: The imaging-app watch and its alerts (stopped checking, back, disconnected) and the send
  mode "only while an imaging app is connected" MUST apply to the Dome. The Alpaca page MUST list the
  Dome with its live state, and the dashboard MUST show its imaging-app tile, using the existing
  components (no new UI parts).

**Docs**

- **FR-014**: The Alpaca user guide MUST describe the Dome: what each member does, the refusals and
  their errors, the stop rule, that ConformU against the real roof is only for bench mode or with a
  person at the roof, and how to add it in N.I.N.A.; DIA-18 MUST show the Dome's commands and
  refusals. The REST and interface references, the README, the docs home page and the development
  workflow MUST stop saying there is no Alpaca device.

### Key Entities

- **Dome status**: the shutter state (open, closed, opening, closing, error) and whether it is slewing,
  derived from the roof's state and whether a close is waiting.
- **Dome command**: open, close or abort, mapped to the roof's open, close or stop with the client
  origin.
- **Dome answer**: accepted, or refused with the text the web UI would show.
- **Command origin**: person (the authenticated web UI), automatic (spec 005), client (Alpaca).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: ConformU's conformance and Alpaca protocol checks report zero errors, zero issues and
  zero configuration alerts against the simulator's Dome, locally and in CI.
- **SC-002**: Every refusal in User Story 2 has a native test that fails if the Alpaca path skips that
  check, and each shows the motor was never driven.
- **SC-003**: Every Dome member in FR-002 to FR-004 and FR-009 has a native test of its reply,
  including each shutter state mapping (7 roof states, with and without a waiting close).
- **SC-004**: `AbortSlew` turns the motor off within one control step, connected or not, except during a
  close while the weather isn't safe, which it can't stop; it is never counted as a person's stop (native tests of
  both rules).
- **SC-005**: On the bench (motor disconnected, bench check): N.I.N.A. connects to the Dome, opens and
  closes it with the weather safe and the mount parked, sees the refusal text when unsafe, and its
  Stop button stops a move.
- **SC-006**: The full local check (firmware build, native tests, web tests, Playwright, quality gate,
  docs build, flash budget) passes with no new warnings, and the flash image grows by less than 15 KB.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **An Alpaca client is not a person.** It can't move the roof from an unknown position and can't
   clear a fault; a person does both from the web UI. The cost: after a boot between the magnets,
   N.I.N.A. can't open or close the roof until someone moves it to a magnet.
2. **A client can't stop a close while the weather isn't safe**, whoever started it, and no client
   stop ever counts as a person's stop, so nothing an unauthenticated client sends can call off or
   delay the roof closing in bad weather. The cost: someone at the roof who must stop it uses the web
   UI's Stop (any page, logged in), not N.I.N.A.'s, and N.I.N.A.'s Stop button shows an error then.
2a. **`AbortSlew` is the roof's Stop**, keeping the position estimate, not its Abort (which would make
   the position unknown and so stop the automatic close from closing by itself).
2b. **A client can't reverse a close**: its open is refused while the roof is closing, a close waits,
   or a close was stopped part-way; a person at the web UI can.
3. **Open and close need a connected client; abort doesn't.** With Alpaca off, the Dome changes
   nothing, `AbortSlew` included; the web UI's Stop remains.
4. **Nothing stops or starts a move on a client's behalf** when it disconnects or goes silent; the
   imaging-app alert reports a silent client.
5. **Stopped part-way, an unknown position or a fault read as error.** Only the closed magnet reads as
   closed and only the open magnet as open.
6. **A waiting close reads as closing** (and `Slewing` true), so an app polling for "closed" keeps
   waiting rather than calling close again.
7. **Refusals use ASCOM's InvalidOperation error** with the same text the web UI shows; a latched fault
   also names the fault with its value and limit.
8. **Alpaca stays off by default.** Turning it on now also lets a client open and close the roof
   within the safety rules; the Alpaca docs and the setting's hint say so.
9. **The demo keeps no Alpaca device** until spec 011 gives it a simulated roof.
9a. **The Dome's name and description are fixed text** ("RoofRoof roof"), not the device name.
10. **Interface version 3** (ASCOM Platform 7: Connect, Disconnect, Connecting, DeviceState).
11. **The simulator starts closed, with the weather safe and the mount parked**, and a short simulated
    travel (a few seconds), so ConformU's shutter tests finish quickly; options start it unsafe or
    unparked for manual tests of the refusals.

## Out of scope

- Roof alert events and their wording (007), MQTT and Home Assistant roof entities (008), the
  dashboard's roof card and a friendlier rendering of the live Dome state (009), Wi-Fi and OTA
  behaviour during a move (010), the demo's simulated roof and bench mode (011), ConformU against the
  real roof (012, commissioning).
