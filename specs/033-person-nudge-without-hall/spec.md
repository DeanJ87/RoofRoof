# Feature Specification: A Person Can Nudge the Roof Without the Hall Sensor

**Feature Branch**: `spec/033-person-nudge-without-hall`

**Created**: 2026-11-25

**Status**: Draft

## Context

A person's jog (spec 019: hold to run, slow duty, keep-alive deadline, its own short limit) is the
manual control at the roof. Today it is refused exactly when it is needed most: when the Hall sensor
has failed. A Hall reading at a rail latches the Hall rail fault, the jog refuses every latched
fault (`faultLatched`), clearing the fault doesn't help (a dead sensor latches it again on the next
step), and a reading that is not settled or at a rail refuses it too (`hallNotSettled`). A roof left
part-open with a dead sensor could not be nudged from the web UI at all.

## Clarifications

### Session 2026-11-25

- Q: Which Hall states let a person's jog run? → A: The Hall rail fault latched (and no other fault:
  the first fault latched is the one kept), or no fault with the Hall reading at a rail or not
  settled (the first moments after start-up). Everything else runs the normal jog of spec 019.
- Q: How does such a jog run? → A: Blind, as the blind close of spec 030 runs: the dead time first,
  then the slow duty toward the end the person holds, on time alone. It runs only while the button is
  held (the same keep-alive deadline, `JOG_HOLD_MS` of step time), and at most the jog's limit
  (`jogLimitMs()`: the smaller of 30 s and a move from an unknown position's timeout). It stops on
  release, Stop, Abort, a stall seen as supply sag (`roof.blindSagVolts`, as the blind close), and
  every fault the controller still checks while the Hall fault is latched (supply below its minimum or
  unreadable, over-current, current unreadable, a late step, the watchdog and park interlock raised
  by the caller). No magnet ends it: the sensor can't see one.
- Q: Which other faults still refuse it? → A: All of them: any latched fault other than the Hall rail
  fault (timeout, did-not-leave, wrong magnet, over-current, current unreadable, supply low, late
  step, drive budget, external: watchdog, park interlock, stored-unreadable, pin setup) refuses the
  jog as today. Those faults don't hide the sensor: a person clears them and then nudges with the
  Hall sensor working. Invalid settings, a supply that is low or unreadable and current sense that
  is on but unreadable also refuse it at the start.
- Q: Who may start one? → A: A person only (the authenticated web UI). Alpaca, MQTT and the
  automatic origins are refused as for every jog. Open, Close and the calibration run are unchanged.
- Q: The position afterwards? → A: Unknown. The Hall rail fault stays latched (state Error after
  the jog); the rail fault's self-clear at rest (spec 027) starts again after the jog.
- Q: What does the person see? → A: The jog panel (Calibrate page, setup wizard's direction and
  teach screens) keeps its hold buttons and, above the Hall reading, a warning: "Nudging without the
  position sensor: watch the roof", saying it won't stop at either end by itself. The dashboard's
  explanation of the Hall rail fault says the roof can still be nudged on the Calibrate page.
- Q: The automatic close and a blind jog? → A: As with a jog (spec 019), the automatic close takes
  over: the jog is stopped first. The blind close (spec 030) stops a person's blind jog before it
  starts. Open, Close and a second jog are refused while one runs.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Nudge a roof whose Hall sensor has failed (Priority: P1)

The Hall sensor has failed with the roof part-open. The owner, at the roof, opens the Calibrate page,
confirms they are at the roof, reads the warning and holds Close; the roof moves slowly while held
and stops when they let go, or after the jog's limit.

**Independent Test**: native tests with the Hall rail fault latched; Vitest with a `hallRail` fault
in the status.

**Acceptance Scenarios**:

1. **Given** the Hall rail fault latched and nothing else wrong, **When** a person holds Close (or
   Open), **Then** the roof drives that way at the slow duty after the dead time, the fault stays
   latched and the position unknown.
2. **Given** a blind jog running, **When** the keep-alives stop, **Then** the motor is off within
   `JOG_HOLD_MS` of step time.
3. **Given** a blind jog held, **When** it reaches `jogLimitMs()`, **Then** it stops (no new fault).
4. **Given** the Hall rail fault, **When** Alpaca, MQTT or the automatic close asks for a jog,
   **Then** it is refused.
5. **Given** any other fault latched, **When** a person holds a direction, **Then** it is refused
   (`faultLatched`) and the buttons stay unavailable with the reason.
6. **Given** the Hall fault, **When** the page shows the jog, **Then** the hold buttons are available
   (after "I am at the roof") with the warning.

### Edge Cases

- A Hall sensor that reads normally again while the rail fault is still latched: the jog still runs
  blind (the fault says the sensor can't be trusted); clearing the fault gives the normal jog back.
- Supply drops while nudging: the supply-minimum check stops it (the Hall fault stays the latched one).
- A stall at the chain's end: the supply-sag stop of spec 030 ends it.
- The weather unsafe, daylight closing or the park not confirmed: the jog's own gates (spec 019) are
  unchanged and still refuse it.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A person's jog MUST run blind when the Hall rail fault is the latched fault, or no fault
  is latched and the Hall reads at a rail or not settled.
- **FR-002**: A blind jog MUST run at the slow duty, after the dead time, toward the held end, only
  while renewed (`JOG_HOLD_MS`), at most `jogLimitMs()`, and stop on release, Stop, Abort, a supply-sag
  stall and every fault the controller checks.
- **FR-003**: Any other latched fault, invalid settings, a low or unreadable supply and an unreadable
  current sense (when on) MUST refuse it.
- **FR-004**: Only `Origin::Person` MAY start one; Alpaca, MQTT and automatic origins are unchanged.
- **FR-005**: A blind jog MUST leave the position unknown and the Hall rail fault latched; it is
  reported as a jog (`jogging`), not as the blind close (`blind`), and never in the blind close's report.
- **FR-006**: Open, Close, the calibration run and a second jog MUST be refused while one runs; the
  automatic close and the blind close stop it first.
- **FR-007**: The web jog panel MUST keep the hold buttons available in that state with the warning
  "Nudging without the position sensor: watch the roof"; other faults still disable them.

### Key Entities

- No new setting, route or status field. The status's `fault.kind`, `hall.level` and `jogging` say
  everything the page needs.

## Design *(mandatory for UI changes)*

One warning Note (DS rule: a card's own problem is a Note, not a Banner) in the existing jog panel;
no new component, no CSS.

## Success Criteria *(mandatory)*

- **SC-001**: With a dead Hall sensor, a person can move the roof either way from the web UI.
- **SC-002**: Every new path is covered by native tests (allowed for a person, refused for others,
  keep-alive stop, time cap, other faults block) and a Vitest check of the buttons and the warning.

## Assumptions decided for the owner

- Constitution 9.0.0 (MAJOR): Principle I gains a third exception, a person's blind jog. Risk: with
  no Hall sensor nothing stops the roof at its ends; the person's eyes, the keep-alive, the slow duty,
  the jog's limit (30 s at most) and the supply-sag stall stop are the only guards.
- Only the Hall rail fault is treated as "sensor failed". A wrong-magnet, timeout or drive-budget
  fault may come from a sensor problem too, but a person can clear those and jog normally; if the
  sensor is dead, the clear turns into the Hall rail fault and the blind jog.
- No extra limit across repeated blind jogs (the drive budget counts only moves with a working
  sensor): each one is held by a person watching the roof.
- The blind jog uses the blind close's stall stop (`roof.blindSagVolts`), so a roof driven into an
  end stops there too.
- The automatic close and the blind close win over a person's blind jog, as they win over a jog.
- "The roof page" is the Calibrate page, where the jog lives; the dashboard's Hall rail explanation
  points there. The setup wizard's direction and teach screens use the same panel.
- The UI change is a single Note in an existing panel, reviewed in the PR rather than as a mockup.

## Left for the owner (bench)

- Unplug the Hall sensor with the roof part-open: the Hall rail fault latches; the Calibrate page
  shows the warning and the hold buttons; holding Close moves the roof slowly, letting go stops it.
- Hold to the chain's end: the supply-sag stop or the jog limit ends it.

## Out of scope

- Nudging while the weather is unsafe, daylight closes or the park isn't confirmed (spec 019's gates).
- Alpaca, MQTT and the automatic close; a jog on the dashboard itself.
