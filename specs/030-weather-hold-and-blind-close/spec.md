# Feature Specification: Weather Hold, Closing Without a Known Position, and Never Sitting Silent

**Feature Branch**: `spec/030-weather-hold-and-blind-close`

**Created**: 2026-11-25

**Status**: Draft

## Context

Live testing on the real roof (after specs 027 and 028) found that the roof reacts too fast to a
blip and too little to a real failure:

1. **Blips move the roof.** The automatic close starts about 5 s after the weather verdict turns
   unsafe. SQMeter browns out and restarts now and then; a 10 s silence or one unsafe reading should
   never move the roof. Rain is different: it must close at once.
2. **A part-open roof sits.** After a crash 10 s into an open, the board restarted with the roof
   about 10 % open, the Hall sensor healthy (reading "between") and the position unknown. The weather
   then turned unsafe and the automatic close sat in "can't move" for minutes: constitution I forbids
   moving by itself from an unknown position. With the Hall sensor itself dead, the same happens.
3. **Silence.** A restart with the position unknown or a fault latched, a Hall sensor that fails at
   2 am, and the "can't move" phase can all go unannounced, or be announced once into an outage.
4. **Alerts lost in outages.** An alert that fails to send (the access point restarting, no internet,
   Pushover unreachable) is lost, or retried only in memory, so a power cut loses it.
5. **Codes, not words.** SQMeter's reasons show as `reasonFlags 0x10`, which means nothing to a
   person; SQMeter's `/api/safety` already sends the reasons as text.
6. **N.I.N.A. gone while it rains.** With N.I.N.A. down, the park can't be confirmed and the roof
   stays open (constitution II: a wet telescope can be dried, a crushed one can't). The owner keeps
   that as the default and wants an opt-in to close anyway after N.I.N.A. has been silent a while.

## Clarifications

### Session 2026-11-25

- Q: What exactly does the weather hold delay? → A: Only the **automatic close's start**. The
  verdict itself turns unsafe at once: Open is refused at once, an opening roof stops at once, the
  status says unsafe at once. The hold counts the unsafe time; the close starts when it reaches
  `autoClose.unsafeHoldSeconds` (default 60 s, 0-300; 0 is the old behaviour).
- Q: Which reasons skip the hold? → A: **Rain** (SQMeter's rain flag, bit 9, from any source, even a
  stale one): the close starts at once. A failed rain sensor (bit 10) is "any other reason": held.
  *(Superseded by fix 042, constitution 10.1.0: a failed rain sensor starts the close at once, as
  rain does; a silent, stale or unreadable source is still held.)*
- Q: A silent SQMeter? → A: The source turns stale after `safety.staleSeconds` (default 30 s) without
  a good reply; the stale (or unreachable, unreadable, offline) source is then held like any other
  reason. A silent SQMeter therefore starts the close after `staleSeconds + unsafeHoldSeconds`
  (default 30 + 60 = 90 s, plus up to one 5 s poll). A 1-15 s restart of SQMeter never even turns the
  source stale; a real outage starts the close within about 95 s. Both settings are documented
  together.
- Q: Anti-flap? → A: The hold **adds up** unsafe time and forgets it only after a safe spell as long
  as the hold. Once it has fired (the close is wanted) it stays fired until the weather has been safe
  for that same spell. So a verdict flapping every 5 s reaches the hold in about 2 minutes, starts one
  close and doesn't call it off on each safe blip. The roof never reopens by itself (unchanged:
  N.I.N.A. opens).
- Q: Weather safe again before the roof moves? → A: Once the weather has been safe for the clear
  spell, the hold lets go and the automatic close's wait (park request, park wait, back-off) is called
  off as spec 005 does today. Once the motor is closing, the close finishes (close wins).
- Q: A person's Open override (spec 027) and the hold? → A: An override in effect resets the hold,
  so the overridden reasons never start a close behind the person's back.
- Q: A restart in the middle of a hold or a close? → A: A sequence that the restart restored mid-
  attempt (spec 005's RTC record) starts with the hold fired; a fresh boot starts with the hold
  empty, and the start-up window (sources "waiting") counts as doubt, held like the rest.
- Q: What does the automatic close do from an unknown position with a working Hall sensor? → A:
  (owner decision, constitution 8.0.0 I) the same as a person's move from an unknown position: at the
  slow duty, stopping at the first magnet, with the hard timeout of a move from an unknown position
  (full travel at the slow speed plus the margin), only for unsafe weather or daylight that wants the
  roof closed, and only once the park is confirmed (or the N.I.N.A.-lost rule, below). A wrong magnet
  is a fault as for every move. Afterwards a critical alert says the position had been unknown.
- Q: And with the Hall sensor dead? → A: "Blind close" (`roof.blindClose`, on by default and after
  an upgrade, by owner decision): with a Hall rail fault latched (sensor reading at a rail), the
  automatic close drives towards closed on time alone: slow duty, hard limit = the unknown-position
  timeout (full travel at the slow speed plus the margin), stopping early on a stall seen as supply
  sag (below), on any fault the controller still checks (supply minimum, late step, the park
  interlock) and on Stop. The Hall fault stays latched; the position stays unknown; a critical alert
  asks a person to check the roof. One blind close per fault: the fault record remembers it (NVS) so
  a restart doesn't hammer the chain again.
- Q: Stall detection without current sense? → A: The 12 V supply sags about 1.1 V while the motor
  runs; a stall at the chain's end sags it further. The blind close takes the supply at rest just
  before it drives; after the first second of driving (the start-up surge), a sag of at least
  `roof.blindSagVolts` (default 2.0 V, 0.5-6.0) held for 500 ms stops the motor. Current sense is not
  fitted; the supply-minimum fault (default 11.0 V for 250 ms) stays a second, harder stop.
- Q: What is "a person acts" for the "needs a person" alert? → A: The condition ending: the fault
  cleared (or a Hall rail at rest clearing itself), the position known again (a magnet read after a
  move, teach, or a person's move). The alert repeats at the reminder interval
  (`autoClose.remindMinutes`) until then.
- Q: Which alert levels? → A: A new event `needs_person` ("Roof needs a person"), critical like
  `mount_not_parked` and `roof_not_closed`: past the pause, the startup grace and the master switch,
  default **Wake me**, never below Urgent. One event covers: a restart with the position unknown, a
  fault latched or the Hall faulted; a restart with the roof not reading closed (sent once, not
  repeated, since an open roof at a known position is not a fault); a Hall rail fault at any time;
  the close sequence in "can't move"; a blind close or an unknown-position close that ran.
- Q: Delivery through an outage? → A: Every alert that no channel took is retried with back-off
  (60 s doubling up to 30 min) until a channel takes it, a newer alert of the same event supersedes
  it, or its condition is resolved. Critical alerts (wake and urgent roof events) waiting for delivery
  are also kept in NVS (one slot per event, written when queued and erased when delivered; a few
  writes per incident, never from an interrupt) and sent again after a restart or power cut, marked
  as sent before the restart. A condition still present after the restart is raised again from fresh
  evaluation as well.
- Q: N.I.N.A. unreachable while a close is wanted? → A: (owner decision) by default never close
  without a confirmed park, whatever the weather, rain included: keep asking N.I.N.A., keep the
  attempt-and-back-off loop running for as long as the close is wanted, and raise the Wake-me alert
  "Can't close: N.I.N.A. unreachable, mount park unknown", reminded until a person resolves it. The
  park source decides what "confirmed" means (spec 024): switches only, the switches alone confirm;
  N.I.N.A. and switches, both must agree (a disagreement blocks and alerts); N.I.N.A. only, N.I.N.A.
- Q: The opt-in to close anyway? → A: `autoClose.ninaLostClose` (off by default) and
  `autoClose.ninaLostMinutes` (default 5, 1-60): with the park source N.I.N.A. or N.I.N.A. and
  switches, unsafe weather wanting the roof closed, and N.I.N.A. unreachable, stale or unreadable
  without a break for that long, the automatic close closes without N.I.N.A.'s park. With N.I.N.A.
  and switches, only while every fitted switch reads closed (the switches stand in alone; a switch
  that isn't closed still blocks). It applies to the automatic close only (never Alpaca, MQTT or a
  person's command) and to its re-check during that close. A Wake-me alert says the roof closed
  without N.I.N.A.'s park. Daylight alone never uses it.
- Q: SQMeter's reasons as words? → A: The reply's `reasons` array (also in its MQTT `<base>/safety`
  payload) is kept with the reply: joined with "; ", control characters dropped, at most 160 bytes.
  The status, the dashboard, Home Assistant and alerts show it. Without it, a built-in table of
  SQMeter's flag names (SQMeter `SafetyEvaluator.h`, bits 0-17) gives the words; an unknown bit reads
  "other reason (bit N)". The raw `reasonFlags` stays in the status for machines.
- Q: Alpaca OpenShutter while unsafe? → A: Already refused (spec 027): the Alpaca error names the
  weather reason. Spec 030 only adds tests, and the reason now reads in words.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A blip never moves the roof, rain always does (Priority: P1)

**Independent Test**: In native tests (and the demo), make SQMeter unsafe for 2 s, 10 s, 59 s, or
silent for 3 s and 15 s: the roof never moves. Unsafe for 61 s: the close starts. Rain: the close
starts at once. Flapping every 5 s for 2 minutes: exactly one close.

**Acceptance Scenarios**:

1. **Given** the roof open and the weather safe, **When** SQMeter reports unsafe (cloud) for 59 s then
   safe, **Then** no park request and no close; Open stays refused while it is unsafe.
2. **Given** the same, **When** unsafe lasts 61 s, **Then** the automatic close starts (park request).
3. **Given** the same, **When** SQMeter reports rain, **Then** the close starts at once.
4. **Given** SQMeter silent for 15 s, **Then** nothing moves; silent for 100 s, **Then** the close starts.
5. **Given** the verdict flapping every 5 s, **Then** exactly one close, and no reopen.
6. **Given** a close waiting for the park, **When** the weather has been safe for the clear spell before
   the motor started, **Then** the wait is called off; **When** the motor is already closing, **Then**
   the close finishes.

### User Story 2 - An unsafe roof closes even when the position is unknown (Priority: P1)

**Independent Test**: The W05 sequence in a native test: a crash 10 s into an open, a restart with the
Hall reading "between", unsafe weather, the park confirmed: the roof closes at the slow duty to the
closed magnet, and a critical alert says the position had been unknown. With the Hall at a rail: the
blind close drives for at most the limit, stops early on a stall, and the "check the roof" alert goes.

**Acceptance Scenarios**:

1. **Given** the position unknown and the Hall healthy, **When** the hold fires and the park is
   confirmed, **Then** the roof closes at the slow duty and stops at the closed magnet.
2. **Given** the same with the park not confirmed, **Then** nothing moves; the park is asked for and the
   alert says why.
3. **Given** a Hall rail fault latched and `roof.blindClose` on, **When** the close is wanted and the
   park confirmed, **Then** the roof drives towards closed at the slow duty and stops at the limit or on
   a stall; the fault stays; a critical alert asks a person to check the roof; no second blind close
   follows, also after a restart.
4. **Given** `roof.blindClose` off, **Then** the Hall fault leaves the roof where it is and the critical
   alert says it can't close.
5. **Given** any other fault latched (timeout, wrong magnet, supply), **Then** nothing moves (unchanged).

### User Story 3 - Never silent (Priority: P1)

1. **Given** a restart with the position unknown, a fault latched or the Hall faulted, **Then** the
   `needs_person` alert goes out at once (no startup grace), Wake me, repeated at the reminder interval
   until the condition ends; the dashboard and Home Assistant show it prominently.
2. **Given** a restart with the roof not reading closed, **Then** `needs_person` says so once.
3. **Given** a Hall rail reading at any time, **Then** `needs_person` goes out at once.
4. **Given** the close sequence in "can't move", **Then** a critical alert goes out and repeats.

### User Story 4 - Alerts survive outages (Priority: P1)

**Independent Test**: The access point down → SQMeter stale → close wanted, N.I.N.A. unreachable →
retrying and the alert queued → access point up, weather safe → the close is called off and the queued
alert delivered. The unsafe variant ends in park and close. A restart in the middle sends the kept
alert again.

### User Story 5 - Words, not codes (Priority: P2)

1. **Given** SQMeter reports `reasonFlags` 0x10 and `reasons` ["Cloud 100% >= 30%"], **Then** the
   status, the dashboard, Home Assistant and the alert say "Cloud 100% >= 30%".
2. **Given** no `reasons`, **Then** they say "cloud cover".

### User Story 6 - The owner may let the roof close when N.I.N.A. is gone (Priority: P2)

1. **Given** `autoClose.ninaLostClose` off (default) and N.I.N.A. unreachable in the rain, **Then** the
   roof stays open, the attempt loop keeps asking and the Wake-me alert repeats.
2. **Given** it on, park source N.I.N.A., N.I.N.A. silent 5 minutes, **Then** the roof closes and the
   Wake-me alert says it closed without N.I.N.A.'s park.
3. **Given** it on, park source N.I.N.A. and switches, a switch open, **Then** nothing moves.
4. **Given** park source switches, **Then** the switches confirm the park as before; N.I.N.A. doesn't matter.

### Edge Cases

- A person's Stop during an unknown-position or blind close stands the sequence down as for any
  automatic close (spec 005); the stand-down's alert still goes.
- Settings saved mid-hold apply at the next step; lowering the hold below the count fires it.
- A blind close stopped by a stall or the limit is not a new fault; the Hall fault stays latched.
- The test box's weather bypass never stands in for a configured source (unchanged).
- N.I.N.A.-lost close and the person's "Close anyway" are separate: a person's needs the password.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The automatic close MUST start only once the unsafe verdict's time (summed, reset by a
  safe spell of the hold's length) reaches `autoClose.unsafeHoldSeconds` (default 60, 0-300), except
  rain, which MUST start it at once.
- **FR-002**: Once fired, the hold MUST stay fired until the weather has been safe for the hold's
  length; an Open override in effect MUST reset it; a sequence restored mid-attempt MUST start fired.
- **FR-003**: The verdict, Open's refusal and the stop of an opening roof MUST NOT be delayed.
- **FR-004**: The status MUST report the hold (`roof.weatherHold`: seconds counted, seconds left, fired).
- **FR-005**: The automatic close MUST close from an unknown position with a working Hall sensor at the
  slow duty to the first magnet, with the unknown-position timeout, after the park is confirmed.
- **FR-006**: With `roof.blindClose` on (default) and only a Hall rail fault latched, the automatic
  close MUST drive towards closed at the slow duty for at most the unknown-position timeout, stopping on
  a stall (`roof.blindSagVolts`, default 2.0 V, 0.5-6.0, held 500 ms after the first 1000 ms), any
  fault, or a stop; at most once per latched fault, remembered across restarts.
- **FR-007**: A `needs_person` critical alert MUST go out: at boot (position unknown, a fault latched
  or the Hall faulted; repeated until the condition ends), at boot with the roof not closed (once), on
  a Hall rail fault at any time, while the close sequence can't move, and after an unknown-position or
  blind close.
- **FR-008**: Every alert no channel took MUST be retried with back-off until delivered, superseded
  or resolved; critical ones MUST be kept in NVS and sent again after a restart.
- **FR-009**: By default the roof MUST NOT close without the park confirmed by `park.source`; the
  close keeps being attempted and the Wake-me alert names N.I.N.A. when it is the missing input.
- **FR-010**: With `autoClose.ninaLostClose` on, unsafe weather and N.I.N.A. lost for
  `autoClose.ninaLostMinutes`, the automatic close MUST close without N.I.N.A.'s park (with N.I.N.A.
  and switches, only with every fitted switch closed) and raise a Wake-me alert.
- **FR-011**: SQMeter's `reasons` text MUST be read, kept (sanitised, at most 160 bytes) and shown
  wherever the reason is shown; flags without text MUST read as words.
- **FR-012**: Alpaca OpenShutter while unsafe MUST return an Alpaca error naming the reason (tests).
- **FR-013**: Flash MUST stay at most 95% of the OTA slot with headroom for spec 029.

### Key Entities

- **Weather hold**: unsafe time counted, safe time counted, fired.
- **Blind close report**: ended why (limit, stall, fault, stop), driven ms, supply at rest, deepest sag.
- **Kept alert**: event, level, title, message, first queued time; one NVS slot per critical event.

## Design *(mandatory for UI changes)*

Design-system parts only (constitution VII): the banner (DS-14) for "Roof needs a person" on every
page while `needs_person` holds; `Note` on the roof card for the hold countdown ("Unsafe: closing in
42 s unless it clears") and for a blind or unknown-position close; settings with the existing
controls in the Automatic close and Roof cards; `InfoTip` for each. Copy follows DS-20 to DS-28.

## Success Criteria *(mandatory)*

- **SC-001**: Native tests: unsafe 2/10/59 s and silent 3/15 s never move the roof; 61 s closes; rain
  closes at once; flapping every 5 s for 2 minutes closes exactly once.
- **SC-002**: The W05 sequence (crash mid-open, restart, Hall between, unsafe) ends closed.
- **SC-003**: No restart, fault or "can't move" goes unannounced; alerts queued in an outage arrive
  after it, also across a restart.
- **SC-004**: Flash below spec 027's 94.2% with room for spec 029; LittleFS within 75%; Config.h
  within 600 lines.

## Assumptions decided for the owner

1. **Rain sensor fault is held**, not immediate: only rain itself is "rain". A failed RG-15 makes the
   verdict unsafe at once (Open refused) and closes after the hold. *(Superseded by fix 042: the owner decided a failed
   rain sensor closes at once, since it may have failed because it is wet.)*
2. **Clear spell = the hold** (60 s): one setting for both, so safe must last as long as unsafe had to.
3. **Unknown-position close** stops at the first magnet, like a person's: if it is the open magnet
   (wrong direction setting), that is the existing wrong-magnet fault.
4. **Blind close stall threshold** 2.0 V held 500 ms after a 1 s start: well above the measured 1.1 V
   running sag, so normal travel doesn't stop early; the time limit stays the hard stop. A bench
   measurement should set it.
5. **Blind close is on after an upgrade** (owner decision): the Compatibility rule's "a migration never
   turns on a motion feature" is set aside for it, written into constitution 8.0.0.
6. **One blind close per latched fault**, remembered in the fault record, so a board restarting in the
   rain can't hammer the chain repeatedly.
7. **N.I.N.A.-lost close with N.I.N.A. and switches** needs the switches closed (the safer of the two
   readings of the owner's choice); with N.I.N.A. only, nothing confirms the park.
8. **`needs_person` at boot with the roof open at a known position** goes once, not repeated: the roof
   may be open on purpose (a restart while imaging).
9. **Kept alerts**: one NVS slot per critical event (the newest wins), written only when an alert of
   that event is queued and erased on delivery, so writes stay a handful per incident.
10. **Flash**: link-time optimisation for this project's own code (src/ and lib/), not the framework or
    lib_deps; none of our code runs in an interrupt or IRAM. It needs the bench check below.

## Left for the owner (bench)

- Boot and run the LTO image on the DevKit; one move each way; an alert sent.
- Measure the running sag and a stall sag at the chain's end; set `roof.blindSagVolts`.
- A blind close with the Hall connector pulled (attended, park confirmed).
- The W05 sequence: restart mid-open, then unsafe weather: the roof closes slowly to the magnet.
- Pull the access point during an alert; confirm it arrives after, also across a power cut.

## Out of scope

Crash capture and its root cause (spec 029). Current sensing hardware.
