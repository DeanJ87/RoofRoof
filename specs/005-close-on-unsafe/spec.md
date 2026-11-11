# Feature Specification: Close on Unsafe

**Feature Branch**: `spec/005-close-on-unsafe`

**Created**: 2026-11-11

**Status**: Draft

## Context

Spec 003 runs the roof on the board and checks every command: a close needs a confirmed park
(N.I.N.A. at park AND every fitted park switch closed), an open needs a safe weather verdict, and an
open in progress stops as soon as the verdict stops being safe. Spec 004 produces the weather
verdict and the park result and can ask N.I.N.A. to park the mount. Nothing yet acts on an unsafe
verdict: a roof left open when it starts to rain stays open.

This spec adds the automatic response that constitution III requires. When the weather is not safe
and the roof is not closed, the controller:

1. asks N.I.N.A. to park the mount;
2. waits, with a time limit, for the park to be confirmed by the park rule;
3. closes the roof through the same checked command path as every other close, and waits for it to
   reach the closed magnet.

Each step that can't complete stops the sequence where it is and raises a critical alert naming
the step. The sequence never opens the roof. The roof alert events with their wording, levels and
settings are spec 007; this spec sends the critical ones through the existing alert pipeline with
fixed wording, through one hook that 007 replaces.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: What counts as "unsafe" for starting the sequence? → A: Any verdict that is not "safe",
  including "unknown" (the start-up window). Constitution III: doubt resolves to unsafe. A restart
  with the roof open therefore asks for a park until a source says safe.
- Q: Can a person open the roof while the verdict is unsafe, as an override? → A: No. Constitution
  III refuses opening while the verdict is unsafe and names no override; spec 003's gate already
  refuses it for every origin, and this spec adds no override.
- Q: What happens when a person stops the roof (or presses stop while the sequence is waiting for
  the park) during the automatic sequence? → A: The sequence stands down: no park request and no
  automatic close until the roof is closed (by a person), or the verdict has been safe without a
  break for 10 minutes. A critical alert says the roof is open while unsafe because a person
  stopped the automatic close. An automatic move never overrides a person's stop: the person may be
  at the roof, and constitution I wants every move to have a known reason. Where two principles
  conflict, the constitution's rule is to stop and alert, not guess.
- Q: What if the park isn't confirmed in time? → A: The roof stays open (constitution II), a critical
  alert names the step and the park result's reasons, and no further park request goes out for
  5 minutes. Meanwhile, if the park becomes confirmed (the owner parks the mount by hand), the close
  goes ahead. After 5 minutes the sequence tries again from the park request.
- Q: How long may the park take? → A: A setting, `park.timeoutSeconds`, default 240 s (range
  30-1800): how long a mount takes to park is a fact of the owner's mount. Shorter means an earlier
  alert.
- Q: How often may a park request be sent? → A: At most once every 30 s, whatever the weather does.
  While waiting, the request is repeated every 30 s (a lost request is retried); a verdict that
  flips between safe and unsafe can't send more.
- Q: Does the weather turning safe stop the sequence? → A: Before the close has been commanded, yes:
  the sequence ends, nothing moves and nothing is reopened. Once the close has been commanded, it
  runs to the closed magnet: stopping part-way would leave the roof neither open nor closed.
- Q: The roof's position is unknown (after a restart between the magnets, or an abort), or a fault
  is latched. → A: No park request and no automatic motion (constitution I). A critical alert says
  the roof can't close by itself and why. When a person has made the position known again (or
  cleared the fault) and it is still unsafe, the sequence resumes without a new park request until
  the 5-minute back-off ends, and closes as soon as the park is confirmed.
- Q: How long must the park be confirmed before the close starts? → A: 4 s without a break (two
  N.I.N.A. polls), so a single good reading among bad ones doesn't start a close that the interlock
  would stop as a fault a moment later. The gate still checks the park at the moment the close
  starts, and the interlock throughout it (spec 003).
- Q: Where do the alerts go, and are they held back by the alert pause? → A: The four critical events
  (mount not parked, roof didn't close, roof can't close by itself, automatic close stopped by a
  person) go through the existing dispatcher at the "wake" level, to every channel that is on,
  ignoring the alert pause and the start-up grace, but only when alerts are switched on. The other
  events (sequence started, ended because safe, roof closed) are logged only; spec 007 decides
  whether they alert.
- Q: Does this spec add the sequence to `/api/status`, MQTT or the UI? → A: No. The log and the
  alerts report it; the status document, MQTT and the dashboard are specs 008 and 009, which add
  the sequence state together with the other roof fields.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | The roof never moves by itself while its position is unknown or a fault is latched | FR-010; spec 002's controller refuses an automatic move from unknown |
| I | Every move has a timeout enforced by the motion code | spec 002; FR-008's guard is an extra outer limit only |
| II | Close only onto a confirmed park, checked the same way for every source | FR-005: the automatic close goes through spec 003's gate and interlock |
| II | Park not confirmed: stay open and raise a critical alert | FR-006 |
| III | Unsafe or raining ends parked and closed: stop, park, wait for the park, close | FR-001 to FR-008 |
| III | Every step has a timeout; a timed-out step stops the sequence and alerts naming the step | FR-006, FR-008 |
| III | The roof never opens by itself; opening is refused while unsafe | FR-012; spec 003's gate |
| III | Doubt about the weather is unsafe | FR-001 |
| III | The alert path doesn't depend on the device that failed | FR-014: the alert never goes through N.I.N.A. or SQMeter |
| III | Every reason states the measured value and limit, or the input that failed | FR-015 |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

The owner is usually asleep or away while this runs. The proof is native tests of the pure
sequence, plus a bench check with the motor disconnected.

### User Story 1 - Rain closes the roof onto a parked mount (Priority: P1)

The roof is open for imaging. It starts to rain. The controller asks N.I.N.A. to park the mount,
waits until the park is confirmed, closes the roof and confirms it reached the closed magnet.

**Why this priority**: This is the reason the device exists (constitution III).

**Independent Test**: The pure sequence, fed an unsafe verdict, an open roof and a park that is
confirmed after some seconds, asks for a park once, commands one automatic close only after the park
has been confirmed for 4 s, and ends when the roof reads closed.

**Acceptance Scenarios**:

1. **Given** the roof open and the verdict safe, **When** the verdict becomes unsafe, **Then** a park
   request goes out at once and the log says the sequence started and why.
2. **Given** the sequence waiting for the park, **When** the park result says parked without a break
   for 4 s, **Then** an automatic close is commanded through the checked command path.
3. **Given** the close running, **When** the roof reads closed, **Then** the sequence ends and the log
   says the roof closed after unsafe weather.
4. **Given** the roof already closed, **When** the verdict becomes unsafe, **Then** nothing is sent
   and nothing moves.
5. **Given** a person's close already running when the verdict becomes unsafe, **Then** no park
   request goes out and the sequence only watches the close to the end.
6. **Given** an open in progress when the verdict becomes unsafe, **Then** spec 003 stops it and the
   sequence parks and closes from where it stopped.

---

### User Story 2 - A park that can't be confirmed leaves the roof open and alerts (Priority: P1)

N.I.N.A. is unreachable, the mount refuses to park, or a park switch stays open. The roof must not
close onto a telescope that may be pointing up (constitution II). The owner gets a critical alert
naming the step and why the park isn't confirmed.

**Why this priority**: Closing onto an unparked mount is the most expensive failure.

**Independent Test**: The pure sequence, fed an unsafe verdict and a park result that never says
parked, never commands a close, raises "mount not parked" exactly when the park time limit is
reached (not one millisecond before), sends no park request for the next 5 minutes, then tries once
more.

**Acceptance Scenarios**:

1. **Given** the sequence waiting, **When** the park isn't confirmed within `park.timeoutSeconds`,
   **Then** the roof stays open and a critical alert says the mount didn't park, with the park
   result's reasons.
2. **Given** N.I.N.A. not configured, **When** the sequence starts, **Then** the alert goes out at
   once (there is nothing to wait for) and the roof stays open.
3. **Given** the alert raised, **When** the owner parks the mount by hand and the park is confirmed,
   **Then** the roof closes without waiting for the next attempt.
4. **Given** the park confirmed for 2 s and then lost, **Then** the 4 s count starts again and no close
   is commanded.
5. **Given** the park confirmed and a close commanded, **When** spec 003's gate refuses it because
   the park was lost at that moment, **Then** the sequence goes back to waiting for the park, within
   the same time limit.

---

### User Story 3 - A close that fails is reported (Priority: P1)

The close faults (a timeout, the park interlock, a jam), is refused by the motion logic (supply low),
stops part-way, or doesn't finish within its own limit. The roof stays where it stopped and the owner
gets a critical alert saying the roof didn't close and why.

**Why this priority**: A roof left part-open in the rain needs a person.

**Independent Test**: The pure sequence, fed a roof that goes from closing to error, or a close
refusal, or a close that never ends, raises "roof didn't close" with the cause, and (for the guard)
commands a stop.

**Acceptance Scenarios**:

1. **Given** the close running, **When** the roof latches a fault, **Then** a critical alert says the
   roof didn't close, with the fault, and the sequence waits for a person.
2. **Given** a close commanded, **When** the motion logic refuses it (supply low, settings invalid),
   **Then** a critical alert gives the refusal's text and the sequence tries again after 5 minutes.
3. **Given** the close accepted, **When** the roof hasn't read closed within the move's own timeout
   plus 30 s (or hasn't started within 30 s of being accepted as waiting), **Then** the sequence
   commands a stop and alerts.
4. **Given** the close accepted, **When** the roof ends neither closed nor in error (a waiting close
   dropped), **Then** a critical alert says so and the sequence tries again after 5 minutes.

---

### User Story 4 - A person's stop is respected (Priority: P1)

The owner is at the roof, or watching, and stops the automatic close (or stops while the sequence
waits for the park). The controller doesn't restart the close behind their back.

**Why this priority**: Constitution I: a move needs a known reason; a person's stop is a reason not
to move.

**Independent Test**: The pure sequence, fed a person's stop at any point, sends no further park
request and no close until the roof reads closed or the verdict has been safe for 10 minutes without
a break, and raises one critical alert.

**Acceptance Scenarios**:

1. **Given** the close running, **When** a person sends stop, **Then** the roof stops, the sequence
   stands down and a critical alert says the roof is open while unsafe because a person stopped it.
2. **Given** the sequence stood down, **When** the verdict stays unsafe for hours, **Then** nothing
   is sent and nothing moves.
3. **Given** the sequence stood down, **When** a person closes the roof, **Then** the sequence ends.
4. **Given** the sequence stood down, **When** the verdict is safe for 10 minutes without a break
   and later turns unsafe, **Then** the sequence runs again from the start.
5. **Given** the sequence stood down, **When** the verdict is safe for 9 minutes and then unsafe
   again, **Then** it stays stood down.

---

### User Story 5 - Never move by itself from an unknown position (Priority: P1)

After a restart between the magnets, or with a fault latched, the roof can't close by itself. The
controller doesn't try; it tells the owner.

**Why this priority**: Constitution I.

**Independent Test**: The pure sequence, fed an unsafe verdict and an unknown or faulted roof, sends
no park request and no close, and raises one critical alert after the position has been unknown for
2 s (so the moment after a restart, before the Hall reading settles, raises nothing).

**Acceptance Scenarios**:

1. **Given** a restart with the roof between the magnets and the verdict unsafe, **Then** no park
   request goes out, nothing moves and a critical alert says the roof's position is unknown so it
   can't close by itself.
2. **Given** a fault latched and the verdict unsafe, **Then** the same, naming the fault.
3. **Given** that alert, **When** a person moves the roof to a magnet (or clears the fault) and the
   verdict is still unsafe, **Then** the sequence resumes: it closes as soon as the park is
   confirmed, and sends a park request again after the 5-minute back-off.

---

### User Story 6 - The weather clears mid-sequence (Priority: P2)

A shower passes while the mount is parking. The controller abandons the sequence; it never reopens
anything.

**Why this priority**: Avoids needless closes; never reopens (constitution III).

**Independent Test**: The pure sequence, fed a safe verdict at each point of the sequence, ends
before the close without a command, or lets a commanded close finish.

**Acceptance Scenarios**:

1. **Given** the sequence waiting for the park, **When** the verdict becomes safe, **Then** the
   sequence ends with no close and no further park request, and the log says so.
2. **Given** the close running, **When** the verdict becomes safe, **Then** the close runs to the
   closed magnet.
3. **Given** the verdict flipping between safe and unsafe every second, **Then** park requests go
   out at most once every 30 s.

### Edge Cases

- The verdict is unknown at start-up (no source has answered yet): it counts as unsafe.
- A person's close while the sequence waits for the park: the sequence watches that close instead of
  sending its own.
- The roof closes because of a person's command while the sequence is in any state: the sequence
  ends.
- A close accepted as waiting (spec 003: an upload or MQTT connect is finishing) counts as the close
  running; if it is dropped, the sequence alerts and tries again.
- A long uptime: the sequence's time base is 64-bit milliseconds since start, which never wraps.
- The park setting changes during a wait: the new limit applies from the next attempt.
- N.I.N.A. answers "already parked": acceptance never counts as parked; only the park rule does.
- The main loop is held up (a blocking reconnect): the sequence's steps are late, never early, and
  spec 003's gates refuse a close on a stale park result.
- A person's stop while the sequence is idle (weather safe) has no effect on a later unsafe episode.
- A fault latched while waiting for the park (for example 12 V low): the sequence stands down as for
  an unknown position and alerts.

## Requirements *(mandatory)*

### Functional Requirements

**Starting**

- **FR-001**: When the weather verdict is anything but "safe" (unsafe or unknown) and the roof is not
  closed, the sequence MUST start, unless it is stood down (FR-009, FR-010).
- **FR-002**: When the roof is already closing (a person's close, or a waiting close) as it starts,
  the sequence MUST only watch that close (FR-007, FR-008) without a park request.

**Park**

- **FR-003**: The sequence MUST ask N.I.N.A. to park when it starts, and again every 30 s while it
  waits. Two park requests MUST never be sent less than 30 s apart, whatever the verdict does.
- **FR-004**: The park counts as confirmed only when spec 004's park result has said "parked" without
  a break for 4 s. N.I.N.A.'s acceptance of the request never counts.
- **FR-005**: The close MUST be commanded with the automatic origin through spec 003's command path
  (the park gate at the start and the park interlock throughout). If that gate refuses it because the
  park was lost, the sequence MUST go back to waiting, within the same time limit.
- **FR-006**: If the park isn't confirmed within `park.timeoutSeconds` of the first request of the
  attempt (at once when N.I.N.A. isn't configured), the sequence MUST raise "mount not parked" with
  the park result's reasons, leave the roof as it is, send no park request for 5 minutes, close if
  the park becomes confirmed meanwhile, and then start a new attempt.

**Close**

- **FR-007**: The sequence ends when the roof reads closed.
- **FR-008**: The sequence MUST raise "roof didn't close", stating the cause, when:
  - the roof latches a fault after the close was commanded (then it stands down as FR-010);
  - the motion logic refuses the close (then it waits 5 minutes as FR-006);
  - the roof stops neither closed nor in error (then it waits 5 minutes);
  - the close hasn't reached the closed magnet within the move's own timeout plus 30 s, or a waiting
    close hasn't started within 30 s: the sequence MUST then command a stop, and wait 5 minutes.

**Standing down**

- **FR-009**: A person's stop or abort, accepted while the sequence is running or waiting, MUST stand
  the sequence down: no park request and no close until the roof reads closed or the verdict has
  been "safe" without a break for 10 minutes. Standing down raises "automatic close stopped by a
  person" once.
- **FR-010**: While the roof's position is unknown (for 2 s without a break) or a fault is latched,
  the sequence MUST send no park request and command nothing, and raise "roof can't close by itself"
  with the reason once. When the position is known again and no fault is latched, the sequence MUST
  resume as after a failed attempt (FR-006's 5-minute wait, closing if the park is confirmed). It
  ends when the roof reads closed or the verdict has been safe for 10 minutes without a break.

**Weather clearing**

- **FR-011**: A "safe" verdict before the close is commanded MUST end the sequence: no close and no
  further park request. A close already commanded MUST be left to finish.

**Never open**

- **FR-012**: The sequence MUST NOT open the roof, and no state of it may lead to an open. Opening
  stays refused while the verdict is not safe (spec 003); there is no override.

**Alerts and logging**

- **FR-013**: Every start, end, attempt and event of the sequence MUST be logged with its reason.
- **FR-014**: The four critical events (mount not parked, roof didn't close, roof can't close by
  itself, automatic close stopped by a person) MUST go out through the alert dispatcher at the
  "wake" level to every channel that is on, ignoring the alert pause and the start-up grace, when
  alerts are switched on. The alert MUST NOT depend on N.I.N.A. or SQMeter being reachable. Spec 007
  replaces the fixed wording with its own events, levels and settings through the same hook.
- **FR-015**: Every alert and log line MUST state why: the park result's reasons with their values
  (e.g. `N.I.N.A. not responding`), the fault with its value and limit, the refusal's text, or the
  time limit that ran out.

**Settings**

- **FR-016**: The settings model MUST gain `park.timeoutSeconds` (default 240, range 30-1800), with
  the same range rule and message on the device and in the web UI. Settings saved before this spec
  MUST load with the default.

**Wiring**

- **FR-017**: The sequence MUST be stepped on the main loop after each safety evaluation hand-over
  (at least every 250 ms), from the verdict and park result of spec 004 and the roof's snapshot from
  spec 003. It MUST NOT block. The roof MUST count a person's accepted stops and aborts so the
  sequence can tell them apart from its own.

**Docs**

- **FR-018**: The user guide MUST describe the sequence, its time limits, what each alert means and
  what to do; a close-sequence diagram MUST show it (a safety diagram, constitution IX); the
  configuration reference MUST document `park.timeoutSeconds`.

### Key Entities

- **Sequence inputs**: the time, whether the verdict is safe, the roof's state, whether a close is
  waiting, the move's timeout, whether the park result says parked, whether N.I.N.A. is configured,
  and the count of a person's stops.
- **Sequence phase**: idle, waiting for the park, closing, waiting before the next attempt, stood
  down by a person, stood down because the roof can't move by itself.
- **Decision**: at most one action (ask for a park, close, stop; never open) and at most one event per
  step.
- **Event**: started, ended (safe), closed, mount not parked, roof didn't close (fault, refused,
  interrupted, too long), roof can't close by itself (position unknown, fault latched), stopped by a
  person.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every transition of the sequence and every time limit has a native test at one below,
  at and one above its boundary.
- **SC-002**: A property test runs at least 5,000 random sequences of weather, park, roof and person
  events and never sees: an open; a close without the park confirmed for 4 s; a park request less
  than 30 s after the last; an action while stood down; an action with the position unknown or a
  fault latched. It also checks that an unparked wait raises "mount not parked" by its limit.
- **SC-003**: Deliberately breaking each rule of the sequence (at least 12 separate breakages) makes
  at least one test fail.
- **SC-004**: On the bench (motor disconnected, bench check): with a simulated unsafe verdict and a
  parked mount, the close is commanded within 5 s of the park being confirmed for 4 s; with
  N.I.N.A. switched off, the critical alert arrives within the park time limit plus 5 s.
- **SC-005**: The full local check (firmware build, native tests, web tests, Playwright, quality gate,
  docs build, flash budget) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **"Unknown" counts as unsafe** for starting the sequence. A restart with the roof open asks
   N.I.N.A. to park until a weather source says safe.
2. **No open override while unsafe.** The constitution names none.
3. **A person's stop stands the sequence down** until the roof is closed or the verdict has been safe
   for 10 minutes without a break, with one critical alert. The automatic close never overrides a
   person's stop. The cost: a stop pressed by mistake leaves the roof open in the rain until someone
   acts on the alert.
4. **Park time limit `park.timeoutSeconds`, default 240 s**, a setting because it depends on the mount.
5. **Park requests at most every 30 s**, repeated every 30 s while waiting; after a failed attempt
   none for 5 minutes, then a new attempt. A close still goes ahead in those 5 minutes if the park is
   confirmed.
6. **The park must hold for 4 s** before the close is commanded.
7. **Weather clearing ends the sequence only before the close is commanded**; a commanded close runs
   to the end.
8. **An unknown position or a latched fault**: no park request and no motion, one critical alert
   (after 2 s of unknown, so a restart's first moments raise nothing). The sequence resumes with the
   5-minute back-off once a person has made the position known or cleared the fault: a person who
   clears a fault while it is unsafe should expect the roof to close once the mount is confirmed
   parked.
9. **Outer close guard: the move's own timeout plus 30 s** (30 s for a waiting close to start); on it
   the sequence commands a stop and alerts. The motion logic's own timeout should always fault first.
10. **Critical alerts at the "wake" level, past the alert pause and the start-up grace**, only when
    alerts are switched on; the other events are logged only. Spec 007 adds the events, levels and
    settings.
11. **No status, MQTT or UI output for the sequence in this spec** (specs 008 and 009).
12. **The sequence runs on the main loop**: its timing is limited by the loop (a blocked loop delays
    it; spec 003's gates still refuse a close on a park result older than 5 s).

## Out of scope

- Alert event types, wording, levels and settings for the roof (007), Alpaca (006), the sequence in
  the status document, MQTT and Home Assistant (008), the dashboard and the settings page row for
  `park.timeoutSeconds` (009), the demo's simulated roof (011).
