# Feature Specification: Data Interfaces

**Feature Branch**: `spec/008-data-interfaces`

**Created**: 2026-11-11

**Status**: Draft

## Context

Specs 002 to 007 built the roof: its motion logic, the board glue with REST commands, the weather
and park inputs, the automatic close on unsafe weather, the Alpaca Dome and the roof alerts. The
roof is visible in `/api/status` and `/ws/status`, and it can be commanded from the web UI's REST
API and from Alpaca. Over MQTT the device publishes only its availability and alerts, and Home
Assistant sees only a connectivity sensor and the alerts switch.

This spec finishes the device's data interfaces, following SQMeter's data-interfaces design (its
spec 013):

- **MQTT out**: one retained roof document under the base topic with the roof's state, position,
  motor current, supply voltage, Hall reading, fault, the weather and park checks as the roof sees
  them, and the automatic close's phase. It is the same roof object as `/api/status`.
- **MQTT in**: open, close and stop commands, honoured only when the owner has switched them on,
  and only through the same checks every other command passes. They are a remote command source
  with no login, so they get rules at least as strict as an Alpaca client's.
- **Home Assistant discovery**: a cover entity for the roof, sensors for position, supply, motor
  current and the Hall reading, and a fault binary sensor.
- **REST and WebSocket**: the status document's roof section gains the automatic close's phase and
  the weather and park checks, so the dashboard (spec 009) can show them. OpenAPI, the contract
  schemas and the interface docs are regenerated and updated.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Which command origin do MQTT commands use? → A: Their own, "MQTT", with every rule an Alpaca
  client has: no move from an unknown position, no clearing a fault, a stop that never counts as a
  person's (so it never stands the automatic close down), a stop refused while a close runs or
  waits and the weather isn't safe, and no open that reverses a close. Refusals name MQTT instead of
  an imaging app. MQTT has no login of its own; the broker is the only gate.
- Q: Are MQTT roof commands on by default? → A: No. A new setting, off by default, turns them on;
  config saved by an older release loads with it off. While it is off the device still answers each
  command on the result topic with "switched off", so a misconfigured automation is visible.
- Q: What about a retained command (left on the broker by mistake)? → A: It is never acted on: it
  would replay on every reconnect and could open the roof with nobody asking. A message whose
  retained flag can't be read counts as retained.
- Q: Can MQTT clear a fault or abort? → A: No. Clearing a fault is a person's action (constitution
  I), and only the web UI is a person. Stop is the only stop command; it keeps the position
  estimate.
- Q: One topic per value, or one document? → A: One retained document on `<base>/state`, as
  SQMeter does, whose `roof` object is the status document's `roof` object field for field. Home
  Assistant reads its values with templates.
- Q: How often is the document published? → A: At rest: at once when the roof's state, fault, the
  close phase, the weather or park check or the known-position flag changes (at most once a second);
  otherwise every publish interval (`mqtt.publishIntervalMs`, default 60 s); and at once after every
  (re)connect. While the roof moves, a close waits or the automatic close is attempting (waiting
  for the park, closing, waiting to retry): only when its state, fault, waiting close or attempt
  changes, at most every 5 s, at most twice until it is at rest again, and not at all once a publish
  has failed on that connection. A write to a broker whose link has silently died can hold the main
  loop for seconds, and the roof's checks depend on that loop (spec 010 owns the network under
  motion). The live position while moving is in `/ws/status`; the MQTT document has the estimate at
  the start and the end of each move. The supply-low flag is not a trigger: it comes from one
  unfiltered reading and would flap under motor load.
- Q: Does the Home Assistant cover carry the position? → A: No. The cover shows the state and, with
  commands on, Open, Close and Stop; the position is its own sensor (null while unknown). A cover
  position would invite a set-position command, which the roof doesn't have. The roof's `error` and
  `unknown` states show as Home Assistant's "unknown".
- Q: Which weather and park state goes into the documents? → A: What the roof's own checks use at
  that moment (the verdict and park result handed over by the main loop, counted unsafe and not
  parked when the hand-over is stale), not the raw sources. It is what decides whether a command
  runs.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | The roof never moves by itself while its position is unknown; only a person moves it from unknown | FR-007: MQTT is not a person |
| I | Only a person clears a fault | FR-007, FR-009: MQTT has no clear-fault command |
| II | Every close request is checked the same way, whatever its source, including MQTT and Home Assistant | FR-007: the shared command check |
| III | The roof never opens by itself; opening is refused while the verdict isn't safe | FR-006 (no retained replay), FR-007 |
| III | Unsafe ends closed: nothing remote can call off or delay the automatic close | FR-007: an MQTT stop never stands it down and is refused while a close runs or waits in weather that isn't safe; no reversing a close |
| III | MQTT and the web UI report the verdict and can never change it or the motion decisions | FR-013 |
| III | Every refusal states the measured value and the limit, or the input that failed | FR-008 |
| VIII | MQTT commands are honoured only from the configured broker and topics, and pass the same checks as every other command | FR-004, FR-005, FR-007 |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Watch the roof from Home Assistant and MQTT (Priority: P1)

The owner runs Home Assistant and a broker. With MQTT and discovery on, the roof appears in Home
Assistant as a cover showing open, closed, opening, closing or stopped, with sensors for its
position, the 12 V supply, the motor current (when current sense is fitted) and the Hall reading,
and a problem sensor that turns on when a fault is latched, with the fault's details. Any MQTT
client can subscribe to one retained document with everything the roof reports.

**Why this priority**: Watching the roof from the house is the main reason to connect it to home
automation, and it is read-only, so it can't move anything.

**Independent Test**: Turn on MQTT and discovery, subscribe to the base topic, move the roof from
the web UI and check that the state document and Home Assistant follow, and that a fault turns the
problem sensor on with its text.

**Acceptance Scenarios**:

1. **Given** MQTT connected, **When** the roof starts opening, **Then** the state document shows
   `opening` within a second, and `open` with the position at 100 % within a second of the magnet.
2. **Given** discovery on, **When** the device connects, **Then** Home Assistant lists the cover, the
   position, supply and Hall sensors and the fault sensor under the device; the motor current
   sensor only while current sense is on.
3. **Given** a latched fault, **When** the document is published, **Then** the fault sensor is on and
   its attributes carry the fault's kind, measured value, limit and text.
4. **Given** the position is unknown or the supply unreadable, **When** the document is published,
   **Then** those values are null and Home Assistant shows them as unknown, never as a made-up number.
5. **Given** the broker drops and comes back, **When** the device reconnects, **Then** the document
   and discovery are published again at once.

---

### User Story 2 - Open, close and stop from Home Assistant, safely (Priority: P1)

The owner switches on MQTT roof commands and uses the cover's buttons or an automation. Open, close
and stop work like the Alpaca Dome's: open only while the weather is safe, close only onto a
confirmed park, neither from an unknown position or with a fault latched. Whatever is sent, the
automatic close in unsafe weather can't be stopped, stood down or reversed from MQTT. Every command
gets an answer on a result topic saying whether it ran and, if not, why.

**Why this priority**: It is the new way to move the roof, so its limits are the safety core of this
spec.

**Independent Test**: With commands on, send OPEN, CLOSE and STOP in each state the native tests
cover (unsafe, unparked, unknown position, fault, a close running in unsafe weather, a stopped
close) and check what runs and what the result topic says.

**Acceptance Scenarios**:

1. **Given** commands off (the default), **When** OPEN is sent, **Then** nothing moves and the result
   says MQTT roof commands are switched off.
2. **Given** commands on, the weather safe and the roof closed, **When** OPEN is sent, **Then** the
   roof opens and the result says accepted.
3. **Given** commands on and the weather not safe, **When** OPEN is sent, **Then** it is refused with
   the weather's reason.
4. **Given** commands on and the mount not confirmed parked, **When** CLOSE is sent, **Then** it is
   refused with the park's reason.
5. **Given** the automatic close running (or waiting to start) in unsafe weather, **When** STOP is
   sent, **Then** it is refused with the weather's reason and the roof keeps closing.
6. **Given** the automatic close waiting for the park in unsafe weather, **When** STOP is sent,
   **Then** the sequence carries on: an MQTT stop never counts as a person's stop.
7. **Given** a close running, waiting, or stopped part-way while closing, **When** OPEN is sent,
   **Then** it is refused: MQTT can't reverse a close.
8. **Given** the position unknown or a fault latched, **When** OPEN or CLOSE is sent, **Then** it is
   refused, saying a person must move the roof or clear the fault in the web UI.
9. **Given** an OPEN left retained on the command topic, **When** the device (re)connects, **Then** it
   is ignored and the result says retained commands are never acted on.
10. **Given** any other payload, **When** it arrives, **Then** nothing moves and the result says which
    payloads are accepted.

---

### User Story 3 - The dashboard can show the automatic close and the checks (Priority: P2)

A web UI or script reading `/api/status` or `/ws/status` sees, in the roof section, the automatic
close's phase (idle, waiting for the park, closing, waiting to retry, held by a person, can't move,
halted) and the weather and park checks the roof's commands use, with their reasons. Spec 009's
dashboard builds on these.

**Why this priority**: The dashboard needs it, but the roof works without it.

**Independent Test**: Read `/api/status` while the automatic close waits for the park and check the
phase, the weather verdict and its reason, and the park check and its reason.

**Acceptance Scenarios**:

1. **Given** the weather unsafe and the mount not parked, **When** `/api/status` is read, **Then** the
   roof section shows the phase `waitPark`, the weather `unsafe` with the reason, and park not
   confirmed with the reason.
2. **Given** the phase changes, **When** a `/ws/status` client is connected, **Then** it gets the new
   document within 250 ms, as it does for a state change.
3. **Given** the same moment, **When** REST, WebSocket and MQTT are compared, **Then** their roof
   objects carry the same fields with the same names and units.

---

### Edge Cases

- Broker down: nothing is published or received; on reconnect the document and discovery go out at
  once. Commands sent meanwhile are lost, never queued (a clean session), so nothing stale runs later.
- A command arrives while a close waits for an upload to stop: the same rules as the other sources.
- Duplicate deliveries (MQTT QoS 1): the same command twice has the same effect as once.
- Commands switched off while Home Assistant shows the cover: the cover becomes read-only on the next
  discovery publish; commands sent meanwhile are refused as switched off.
- Current sense off: the motor current is null and its Home Assistant sensor is removed.
- The roof controller didn't start: commands are refused with that reason; the document still goes
  out with what is known.
- Payload case and whitespace: `open`, ` OPEN `, `Open` are all accepted; `OPEN_NOW` isn't.
- The base topic changes: discovery is cleared under the old topic and announced under the new.
- A very long payload: refused as not a command, without echoing it in full.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The device MUST publish a retained roof document to `<base>/state` with the time
  (Unix seconds, 0 and a false clock-valid flag until the clock is set) and a `roof` object that is
  the status document's `roof` object field for field (one serializer).
- **FR-002**: The document MUST be published at once after every connect; at rest, when the roof's
  state, fault, known-position flag, close phase, weather verdict or park check changes (at most
  once a second), and otherwise every publish interval. While the roof moves, a close waits or the
  automatic close is attempting, it
  MUST be published only on a change of state, fault or waiting close, at most every 5 s and at most
  twice until the roof is at rest, and not after a failed publish on that connection. Home Assistant
  discovery MUST NOT be republished while the roof moves or the automatic close is attempting. A
  write to the broker MUST give up within 250 ms while the roof is busy (1 s at rest), all MQTT writes
  and reads in one main-loop pass MUST share 500 ms while busy (reads never wait for the rest of a
  message), and running out MUST drop the connection, so no MQTT write can hold
  the main loop for seconds.
- **FR-003**: Values that can't be read MUST be null, never a made-up number: position while
  unknown, the supply when unreadable, the motor current when current sense is off or unreadable.
- **FR-004**: The device MUST accept roof commands only on `<base>/roof/set` from its configured
  broker, with the payloads `OPEN`, `CLOSE` and `STOP` (case-insensitive, surrounding whitespace
  ignored). Any other payload MUST be refused without effect.
- **FR-005**: MQTT roof commands MUST run only while a setting (off by default, and off for config
  saved by an older release) turns them on. While off, each command MUST be refused as switched off.
- **FR-006**: A retained message on the command topic (or on the alerts pause topic) MUST never be
  acted on, and a message whose retained flag can't be determined MUST be treated as retained.
- **FR-007**: Every MQTT command MUST pass the device's one shared command check with an MQTT origin
  whose rules are at least as strict as an Alpaca client's:
  - open only while the weather is safe; close only while the park is confirmed (and the park
    re-checked throughout the close, as for every close);
  - no move while the position is unknown or a fault is latched;
  - a stop is refused while a close runs or waits and the weather isn't safe, whoever started it;
  - a stop never counts as a person's stop, so it never stands the automatic close down;
  - an open is refused while a close runs or waits, or after a close stopped part-way;
  - there is no MQTT command that clears a fault, aborts, or sets a position.
- **FR-008**: Every command MUST be answered on `<base>/roof/result` (not retained) with the command,
  whether it was accepted, a machine reason and a text that states the measured value and the limit
  or the input that failed, and the roof's state.
- **FR-009**: Every MQTT command and its outcome MUST be logged.
- **FR-010**: With Home Assistant discovery on, the device MUST announce, besides its existing
  entities: a cover for the roof (state from the document; `error` and `unknown` shown as unknown;
  open, close and stop commands only while MQTT roof commands are on, otherwise read-only); sensors
  for the position (%), the supply (V), the Hall reading (counts, diagnostic) and, only while current
  sense is on, the motor current (A); and a fault binary sensor (problem class) whose attributes
  carry the fault. Entities MUST be removed when discovery is switched off or a sensor's hardware is
  switched off, and re-announced when a setting they depend on changes.
- **FR-011**: The status document's roof section (`/api/status`, `/ws/status`, and so the MQTT
  document) MUST add the automatic close's phase, the weather verdict and its reason, and whether the
  park is confirmed and its reason, as the roof's command checks see them. `/ws/status` MUST be sent
  within 250 ms of a phase change, as for a state change.
- **FR-012**: The REST, WebSocket and MQTT references, the configuration reference, the Home
  Assistant guide, the OpenAPI document and the contract schemas MUST be updated to match the device
  field for field.
- **FR-013**: Nothing received over MQTT, other than the three roof commands above, MUST affect the
  roof, the weather verdict or the close sequence (SQMeter's weather topics stay a weather source
  only, as spec 004 set).
- **FR-014**: The setting MUST be in Settings → Network → MQTT with an explanation of what it allows
  and what it never allows, through i18n, reusing the existing toggle and "?" parts.

### Key Entities

- **Roof document**: the time, clock-valid flag and the status document's roof object.
- **Roof command**: open, close or stop, from MQTT, with its retained flag.
- **Command result**: the command, accepted, reason, text and the roof's state.
- **MQTT roof commands setting**: on or off (default off).
- **Discovery entity**: component, object id, name, the template that reads it from the document,
  unit, class, and the condition under which it is announced.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A change of the roof's state reaches the MQTT document within 1 s, and at rest the
  document is never older than the publish interval.
- **SC-002**: In every unsafe-weather close scenario the native tests cover, no MQTT command stops,
  stands down, delays or reverses the close (0 of N).
- **SC-003**: A retained command never moves the roof (every retained case in the tests is ignored).
- **SC-004**: With discovery on, Home Assistant shows the roof's cover and its five or six sensors
  without any manual configuration.
- **SC-005**: The MQTT document is under 1.5 KB in the worst case.
- **SC-006**: The roof objects in REST, WebSocket and MQTT have the same fields, names and units
  (one serializer, checked by the contract schemas).

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **MQTT has its own command origin with the Alpaca client's rules**, none weaker: no move from
   unknown, no clearing faults, a stop that never stands the automatic close down and is refused
   while a close runs or waits in weather that isn't safe, no reversing a close. Only the wording of
   a refusal differs ("MQTT" instead of "an imaging app").
2. **MQTT roof commands are off by default** and need a setting to turn on; an older config loads
   with them off. Home Assistant's cover is read-only until then.
3. **Retained commands are never acted on**, and a message whose retained flag can't be read counts
   as retained. (A command published with the retain flag still reaches the device live once, as
   MQTT delivers it to current subscribers without the flag; only its later replays are refused.)
   The same applies to the alerts pause switch, so a retained pause can't replay on every reconnect.
   A broker bridged to another broker can redeliver a message held there as live; the docs say not
   to keep retained messages on `roof/set` anywhere (every check still applies). Because the MQTT library doesn't pass the flag, the device reads it from the received
   packet; the library version is pinned so that reading stays correct, and a mismatch fails safe.
4. **No clear-fault, abort or set-position over MQTT.** Clearing a fault stays a person's action in
   the web UI.
5. **One retained document**, not one topic per value, and its roof object is the status document's
   (SQMeter's single-serializer rule). Home Assistant reads it with templates.
6. **The Home Assistant cover has no position**; the position is its own sensor. `error` and
   `unknown` show as Home Assistant's unknown, and the fault sensor says why.
7. **The motor current sensor is announced only while current sense is on.**
8. **Results are not retained, and commands are not queued while the device is offline** (a clean
   MQTT session), so nothing stale can run later.
9. **The weather and park shown are what the roof's checks use**, stale hand-over included, not the
   raw sources.
10. **Publish cadence**: on change (at most once a second), the publish interval otherwise, at once
    on connect; while the roof moves, a close waits or the automatic close is attempting, at most two publishes, 5 s apart, on a change
    of state, fault or waiting close, none after a failed one, and no discovery, so MQTT adds as little as
    possible to the main loop while the roof's checks depend on it. The live position stays in
    `/ws/status`.
11. **The new setting reuses the existing MQTT card's toggle and "?" tip** (no new parts or styles,
    constitution VII); the owner's review of the page is a bench check, as the dashboard and Roof
    settings tab are spec 009's.
12. **The fault sensor's attributes carry the fault's kind, cause, measured value, limit and text**,
    so Home Assistant shows the reason (constitution III).

## Out of scope

- The dashboard roof card and the Roof settings tab (009); the demo's simulated roof (011); Wi-Fi and
  OTA under motion and the MQTT reconnect hand-over (010); MQTT over TLS; a set-position command.
