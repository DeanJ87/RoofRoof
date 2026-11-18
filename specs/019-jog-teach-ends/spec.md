# Feature Specification: Jog and Teach the Ends

**Feature Branch**: `spec/019-jog-teach-ends`

**Created**: 2026-11-18

**Status**: Draft

## Context

The roof finds its ends with one SS49E Hall sensor and a magnet at each end, fitted with opposite
poles: the reading is high at one end and low at the other. Three settings turn the reading into
an end: `roof.hallHighEnd`, `roof.hallHighCounts` and `roof.hallLowCounts`. On a new roof they are
guesses. Spec 016's calibration run measures the roof, but each of its legs stops at the first
magnet the current thresholds see, so it can only run once the thresholds already see both
magnets. Today a first-time owner primes them by hand from the live reading with the motor
disconnected and a magnet held near the sensor (commissioning stage 3).

This spec adds an attended **teach** step on the calibration page. A person at the roof **jogs**
it: they press and hold an Open-direction or Close-direction button, and the roof moves slowly
only while the button is held. At each end they say **"This is closed"** or **"This is open"**,
and once part-way **"This is part-way"**. The device records the Hall reading at rest at each of
the three places, averaged over a second, and from them **proposes** which end reads high and the
two thresholds, using the same rules as spec 016's proposal. The person confirms the proposal,
which saves it through the settings save, or discards it. Once confirmed, the roof's position is
known from the magnet it sits on, read with the taught thresholds. The calibration run can then
measure the times.

The same spec rewords constitution Principle II so that it says what the code already does: the
park switches are an optional second check, not a requirement.

## Clarifications

### Session 2026-11-18

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: How does the device know the button is still held? → A: The page sends a keep-alive every
  150 ms while the button is held. The motion code stops the jog when no keep-alive has arrived for
  500 ms (`Roof::JOG_HOLD_MS`). Releasing the button sends a person's Stop at once. The deadline is
  kept by the control step's own clock, so a stalled browser, web server or Wi-Fi link can only
  stop a jog sooner, never prolong it. 500 ms tolerates two lost keep-alives without stuttering,
  and at the default slow duty (40 % of 38 mm/s) the roof travels about 8 mm in it.
- Q: How fast does a jog run? → A: At `roof.slowDutyPercent`, with no ramp, whatever the position.
- Q: How long may one jog run? → A: Its hard timeout is the smaller of 30 s (`Roof::JOG_MAX_MS`)
  and the timeout of a move from an unknown position (the whole travel at the slow duty plus the
  margin). This is never longer than what Principle I already allows, so no constitution exception
  is needed. The drive budget is the one every move has, counted across jogs toward the same end
  until a magnet is read.
- Q: Where does a jog stop by itself? → A: At the first magnet it reaches after leaving the one it
  started on, whichever end that magnet belongs to (constitution I: a move from an unknown position
  stops at the first magnet). The polarity may be the thing being taught, so the jog doesn't judge
  the end by it, and a jog never raises the wrong-magnet fault. A jog that starts on a magnet and
  pushes toward that magnet's own end faults `didNotLeave` after the leave time, as every move does.
- Q: Which checks gate a jog? → A: Both directions need the weather safe, daylight not wanting the
  roof closed and the mount confirmed parked. The motor direction setting may be wrong on a new
  roof, so "Close-direction" may open the roof and "Open-direction" may close it. The park
  interlock watches every jog, and a park lost while jogging is the existing `parkInterlock` fault.
  If the weather or daylight turns while a jog runs, the jog stops. Every refusal of a
  calibration run start applies too: a fault latched, the roof moving, the close sequence busy, a
  close waiting, settings being applied, something blocking an open, and the controller's own
  readiness (supply, current, Hall, settings).
- Q: What else may move the roof during a jog? → A: Nothing. Open and close from a person, an
  imaging app or MQTT are refused with `jogging`. The automatic close stops the jog and then runs
  through its normal checks: the position is unknown after a jog stopped part-way, so it is refused
  and raises its alert, which a person at the roof sees. Stop from anyone stops the jog.
- Q: Does "This is closed" make the position known at once? → A: No. Constitution I says the
  position is known only at a magnet. The person's word is recorded with the reading. The position
  becomes known when the person confirms the proposal: the new thresholds are saved and applied,
  and the roof, still on its magnet, reads as that end. A roof moved after its mark, or a proposal
  discarded, leaves the position to the Hall reading as usual.
- Q: When can a mark be taken? → A: Only at rest: no move active, no jog and no calibration run,
  for at least one second (the reading window). The Hall reading must be settled and not at a
  rail. A new mark replaces the earlier one for that place.
- Q: How are the thresholds proposed? → A: With the run's rules: the ends must read on opposite
  sides of the part-way reading, each at least 300 counts from it. Each threshold must be at least
  the clearance (100 counts or 4 × the noise) beyond the part-way readings, and read at rest at its
  end with a margin (10 counts or 3 × the end's noise). Within that window the teach proposes the
  halfway point between the part-way reading and the end's reading (the hand rule of the
  calibration page), moved into the window if needed. The run's minimum gap and hysteresis follow:
  the thresholds end at least 200 counts apart. If no threshold fits, nothing is proposed and the
  reason says why.
- Q: How is the proposal accepted? → A: `POST /api/roof/teach/confirm` with the proposed values.
  The device checks they are still its proposal, then saves them through the same settings save
  every other change uses (the device's validation, refused while the roof moves or a platform
  action holds it). `POST /api/roof/teach/discard` drops the marks and the proposal. The device
  never saves a proposal by itself.
- Q: Does any of this survive a restart? → A: No. The marks, the proposal and any jog live in
  memory only. Nothing resumes after a restart.
- Q: Where is it in the UI? → A: A Teach card at the top of the calibration page, with the
  attended confirmation, the live Hall reading, the two hold-to-run buttons, a Stop that is always
  shown, the three mark buttons with their readings, and the proposal with Confirm and Discard. A
  link then leads to the calibration run below. The setup wizard gets a "Teach the ends" page before
  the Calibration run page, which explains the step and links to the card. The wizard page itself
  moves nothing.

## Constitution rules this spec relies on

| Principle | Rule | Where it is kept |
|---|---|---|
| I | Only a person may move a roof whose position is unknown, at reduced speed, to the first magnet | FR-001, FR-003, FR-005: started only by an authenticated person who confirms they are at the roof; slow duty; stops at the first magnet |
| I | Every move has a hard timeout enforced by the motion code | FR-004: the jog's timeout (≤ 30 s and ≤ an unknown-position move's) and the keep-alive deadline are both kept by the control step |
| I | Any motion fault stops the motor and locks out motion; a restart never clears it | FR-006: a jog is a move of the existing controller, with every existing fault and the latch |
| I | Position known only at a magnet | FR-014: a confirmed teach makes the position known through the saved thresholds and the magnet the roof sits on |
| I | Settings that change motion, OTA and restarts are refused while moving | FR-009: a jog is a move; the confirm's save goes through the settings save |
| I | Never move by itself; nothing resumes after a restart | FR-010 |
| II | No close unless the mount is confirmed parked; the interlock is re-checked throughout | FR-007: park needed for both directions, interlock on every jog |
| III | No opening while the weather isn't safe or daylight wants the roof closed | FR-007, FR-008: both directions gated; a jog stops when either turns |
| III | Refusals state the value and the limit or the input that failed | FR-011 |
| V | Every safety rule has a native test | FR-019 |
| VII | Stop reachable and never disabled while the roof moves | FR-021 |
| VIII | Endpoints that move the roof or change state call `requireAuth` | FR-018 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Jog the roof by hand (Priority: P1)

The owner has wired a new roof. Standing at it with the mount parked and the weather safe, they
open the calibration page, confirm they are at the roof and press and hold Open-direction. The roof
creeps open at the slow duty and stops the moment they let go.

**Why this priority**: Without a safe way to move a roof whose ends aren't known, nothing else in
this spec can happen.

**Independent Test**: In the demo, hold Open-direction for two seconds and release: the roof moves
while the button is held and stops within one control step of the release.

**Acceptance Scenarios**:

1. **Given** the weather safe, the mount parked, no fault and the person's confirmation, **When**
   they press and hold a direction, **Then** the roof moves that way at the slow duty.
2. **Given** a jog under way, **When** the button is released, **Then** a person's Stop stops the
   roof.
3. **Given** a jog under way, **When** no keep-alive arrives for 500 ms (the tab is closed, Wi-Fi
   drops), **Then** the control step stops the roof.
4. **Given** a jog held for 30 s, **Then** it stops with the existing `timeout` fault.
5. **Given** a jog that reaches a magnet the thresholds see after leaving its start magnet,
   **Then** it stops there.

---

### User Story 2 - Every check still applies (Priority: P1)

The owner tries to jog while it rains, with the mount unparked, with a fault latched, or during a
calibration run. Each time the page says why and nothing moves. While jogging, the park is lost: the
roof stops with the park-interlock fault.

**Why this priority**: Constitution II and III.

**Independent Test**: For each refusal, the jog request is answered 409 with the reason and its
text; native tests drive each in-motion check.

**Acceptance Scenarios**:

1. **Given** the weather not safe, daylight wanting the roof closed, or the mount not parked,
   **When** a jog in either direction is started, **Then** it is refused with that reason.
2. **Given** no attended confirmation, a fault latched, a calibration run active, the close sequence
   busy, a close waiting, settings being applied, or the roof moving, **Then** a jog is refused.
3. **Given** a jog under way, **When** the park stops holding, **Then** the roof stops with the
   `parkInterlock` fault.
4. **Given** a jog under way, **When** the weather turns unsafe or daylight wants the roof closed,
   **Then** the jog stops.
5. **Given** a jog under way, **When** an imaging app or MQTT asks to open or close, **Then** it is
   refused with `jogging`; a Stop from anyone stops the jog.
6. **Given** a jog under way, **When** the device restarts, **Then** nothing moves after the restart.

---

### User Story 3 - Teach the ends (Priority: P1)

At the closed end the owner presses "This is closed"; they jog part-way and press "This is
part-way"; they jog to the open end and press "This is open". The page shows each reading and
proposes which end reads high and the two thresholds, beside the saved values. They confirm, the
values are saved, and the roof now reads as open.

**Why this priority**: This is the feature.

**Independent Test**: In the demo, jog to each place and mark it: the proposal matches the
simulated magnets; Confirm saves it and the roof's state follows.

**Acceptance Scenarios**:

1. **Given** the roof at rest for a second, **When** the person marks a place, **Then** the
   reading at rest (mean, lowest, highest, noise) is recorded for it.
2. **Given** all three marks, **When** the ends read on opposite sides of the part-way reading,
   each at least 300 counts from it, **Then** a high end and two thresholds are proposed.
3. **Given** both ends on the same side, an end within 300 counts of the part-way reading, or no
   threshold that fits, **Then** nothing is proposed and the reason is shown.
4. **Given** a proposal, **When** the person confirms it, **Then** it is saved through the settings
   save, the marks are cleared, and at a magnet the roof's state and position follow at once.
5. **Given** a proposal, **When** the person discards it, **Then** nothing changes.
6. **Given** the roof moving or jogging, **When** a mark is asked for, **Then** it is refused.

---

### User Story 4 - From the wizard (Priority: P2)

During setup, before the Calibration run page, the wizard shows a "Teach the ends" page that says
what the step is for and what it needs, with a button that opens the Teach card. The Park and rain
inputs page says the park switches are an optional second check.

**Independent Test**: The wizard's Teach page renders no motion control and links to the Teach
card; the inputs page shows the optional note.

---

### User Story 5 - Try it in the demo (Priority: P3)

A visitor holds Open-direction on the demo's calibration page, watches the simulated roof creep,
marks the three places, and confirms the proposal.

**Independent Test**: The demo's handlers answer the jog and teach routes from the demo core; a
Playwright walk-through jogs, marks and confirms.

### Edge Cases

- The tab is closed or the phone locks while a button is held: the keep-alives stop and the roof
  stops within 500 ms.
- The release's Stop is lost on the network: the keep-alive deadline stops the roof.
- A keep-alive arrives after its jog has ended: it is refused (`jogEnded`) and never restarts the
  roof; the person presses again.
- Two browsers: a keep-alive names its jog, so a second browser can't keep another's jog alive;
  a second start while a jog runs is refused (`moving`).
- The motor direction setting is wrong: Open-direction closes the roof. Both directions need the
  park and the safe weather, so this is no less safe than a close; the person sees it and fixes
  `roof.openDirection`.
- The thresholds already see a magnet before the roof reaches its end stop: the jog stops there. A
  further jog toward that end drives on until release, the end stop, or `didNotLeave` after the
  leave time.
- The thresholds see no magnet at all: nothing stops the jog but the person, the 30 s limit and
  the drive budget.
- The ends are marked the wrong way round (the person says closed at the open end): the proposal
  then names the wrong high end. The bench check and the calibration run (which stops at each end
  by direction) show it; the person teaches again.
- Settings saved by someone else between the proposal and its confirm: the confirm saves only the
  three Hall values over the current settings.

## Requirements *(mandatory)*

### Functional Requirements

**Jog**

- **FR-001**: A jog MUST start only from `POST /api/roof/jog` from an authenticated person
  (`requireAuth`, the `X-RoofRoof: 1` header), with `attended: true` and a direction (`open` or
  `close`). No other path (Alpaca, MQTT, Home Assistant, the close sequence, a schedule, the wizard
  page) may start one.
- **FR-002**: The same route with the jog's `id` MUST renew it. A renewal for a jog that has ended,
  or a different one, MUST be refused (`jogEnded`) and MUST NOT move the roof.
- **FR-003**: A jog MUST drive at the slow duty toward the requested end (through
  `roof.openDirection`), with no ramp.
- **FR-004**: The motion code MUST stop a jog when it hasn't been renewed for `JOG_HOLD_MS` (500 ms)
  of control-step time, counted from its start. Its hard timeout MUST be the smaller of
  `JOG_MAX_MS` (30 s) and an unknown-position move's timeout; reaching it is the `timeout` fault.
  The drive budget MUST be every move's.
- **FR-005**: A jog MUST stop at the first magnet it reads after leaving the one it started on,
  whichever end's magnet it is. It MUST NOT raise `wrongMagnet`.
- **FR-006**: A jog MUST be a move of the existing controller: the dead time, the leave time, the
  supply, current and Hall-rail checks, the late-step check, the watchdog and the latch all apply.
  A jog always leaves the position unknown unless it ends at a magnet.
- **FR-007**: A jog in either direction MUST be refused unless the weather is safe, daylight
  doesn't want the roof closed and the park holds. The park interlock MUST watch every jog.
- **FR-008**: A jog MUST stop (not a fault) when the weather stops being safe or daylight wants the
  roof closed.
- **FR-009**: A jog MUST be refused while a fault is latched, the roof moves (another jog
  included), a calibration run is active, the close sequence isn't idle, a close waits, settings are
  waiting to be applied, something blocks an open (an upload, a settings save, an MQTT connect, a
  Wi-Fi change, a restart, the setup hotspot), or the controller would refuse a move. While a jog
  runs, everything held while the roof moves is held.
- **FR-010**: A jog MUST live in memory only; after a restart nothing moves.
- **FR-011**: Every refusal MUST carry a machine reason and a text with the value and the limit or
  the input that failed, using the existing texts.
- **FR-012**: While a jog runs, open and close from a person, a client or MQTT MUST be refused with
  `jogging`; the automatic close MUST stop the jog first and then go through its normal checks; any
  accepted Stop MUST stop it.

**Teach**

- **FR-013**: `POST /api/roof/teach/mark` with `at` (`closed`, `open` or `partWay`) MUST record the
  Hall reading over the last second (`REST_WINDOW_MS`, 50 control steps) as count, mean, lowest,
  highest and noise. It MUST be refused unless the roof has been at rest that whole second (no
  move, no jog, no run) and the Hall reading is settled and not at a rail.
- **FR-014**: With all three marks the device MUST propose `hallHighEnd`, `hallHighCounts` and
  `hallLowCounts`, or refuse with a reason: `samePole` (both ends on the same side of the part-way
  reading), `weakEnd` (an end within 300 counts of it) or `noRoom` (no threshold both clear of the
  part-way readings and read at rest at its end). Each threshold MUST lie in the window the
  calibration run uses, at the halfway point between the part-way and the end's reading when that
  point is inside the window, else at the window's nearer edge, and within 100-3995.
- **FR-015**: `POST /api/roof/teach/confirm` with the three proposed values MUST save them over the
  current settings through the settings save, only when they equal the current proposal (else 409
  `proposalChanged`, or `noProposal`). On success the marks and the proposal MUST be cleared. Once
  the saved settings reach the controller, its position follows the Hall reading as at any rest.
- **FR-016**: `POST /api/roof/teach/discard` MUST drop the marks and the proposal and change no
  setting.
- **FR-017**: `GET /api/roof/teach` MUST return the jog (active, id, direction, elapsed time, its
  limit, the keep-alive deadline and interval), the reading window (ready, mean), the three marks,
  and the proposal or the reason for none. It only reads (no auth, like `/api/status`).
- **FR-018**: `/jog`, `/teach/mark`, `/teach/confirm` and `/teach/discard` MUST call
  `requireAuth` and need the command header. A refusal is 409 with `reason` and `error`; a malformed
  body is 400.
- **FR-019**: The jog's limits, its keep-alive, its stops, the refusals, the reading window, the
  marks and the proposal MUST be pure logic in `lib/` with native tests for each rule and boundary.

**Web UI**

- **FR-020**: The calibration page MUST start with a Teach card: what it needs and whether each
  holds now, the attended confirmation, the live Hall reading on its scale, Open-direction and
  Close-direction hold-to-run buttons (pointer and keyboard: held while pressed, released on
  release, pointer cancel, leaving the button, losing focus or the page being hidden), the three
  mark buttons with their readings, and the proposal beside the saved values with Confirm and
  Discard, then a link to the calibration run.
- **FR-021**: While the roof moves, the card MUST show a Stop button that is never disabled (the
  Stop bar shows too).
- **FR-022**: The setup wizard MUST have a "Teach the ends" page before the Calibration run page,
  with no motion control, linking to the Teach card. The Park and rain inputs page MUST say the
  park switches are optional.
- **FR-023**: Every new text MUST go through i18n with its context note; the page MUST work at
  phone width and pass the accessibility scan.

**Demo, constitution, docs**

- **FR-024**: The demo's simulated roof MUST support jog and teach end to end through the same
  library; bench mode gets it through the same firmware code.
- **FR-025**: Constitution Principle II MUST say "N.I.N.A. reports `AtPark` AND every fitted park
  switch (`park.switches`) is closed", with disagreement or an unreadable source still meaning not
  parked, as a PATCH amendment. The roadmap, docs and wizard MUST present the switches as an
  optional second check. The park rule's behaviour MUST NOT change.
- **FR-026**: The calibration guide, the commissioning guide (stage 3 and the checklist), the REST
  reference and the OpenAPI document MUST describe jog and teach.

### Key Entities

- **Jog**: a person's slow move toward one end, alive only while renewed; its id, direction,
  limit and keep-alive deadline.
- **Mark**: where the person says the roof is (closed, open, part-way) and the Hall reading at rest
  there.
- **Teach proposal**: the high end and two thresholds, or the reason for none.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (the Teach card is a card), DS-02 (each precondition as a pill), DS-03
  (readings as `ReadingRow`s, the Hall reading on the existing scale), DS-04 ("?" hints), DS-06
  (outcomes in the card), DS-07 (problems as Notes), DS-09 (phone width), DS-20 to DS-28 (copy).
  Only the shared parts in `ui.tsx` and `settings/controls.tsx`; Stop is the existing danger button.
- **Mockup**: not reviewed; built from existing cards, rows, notes and buttons only. The owner
  reviews it in the demo before merging (listed under the PR's pending checks).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In the native tests, a jog with no keep-alive stops within 500 ms plus one control
  step of its last renewal, and never runs past 30 s.
- **SC-002**: In the native tests, every refusal in FR-007 and FR-009 refuses a jog in both
  directions, and the park lost mid-jog latches `parkInterlock` in both directions.
- **SC-003**: In the demo, jogging to each place and marking it proposes thresholds that the
  simulated magnets read past at rest and the part-way reading stays clear of, and Confirm makes
  the roof's state follow.
- **SC-004**: No path but the authenticated jog route can start a jog (tested for Alpaca, MQTT and
  the automatic close).
- **SC-005**: The full local check (firmware build, native tests, web tests, Playwright with the
  accessibility scan at phone width, quality gate, docs build, flash and UI budgets) passes with no
  new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Keep-alive deadline 500 ms, keep-alives every 150 ms.** Two can be lost without a stop; the
   roof moves about 8 mm at the default slow duty in that time. Kept by the control step, so the
   network can only shorten a jog.
2. **Release sends a person's Stop** through the existing stop route, so the release and every
   other Stop are the same command.
3. **A jog is capped at 30 s** (and at an unknown-position move's timeout, if smaller). A full slow
   travel takes several presses; the drive budget across presses is every move's. No constitution
   exception is needed.
4. **Both directions need the safe weather, no daylight close and the park**, and the interlock
   watches both: the motor direction setting may be wrong on a roof being taught. In bad weather a
   person closes with the normal Close.
5. **A jog stops when the weather or daylight turns**, in either direction, and the automatic close
   then finds the position unknown and alerts. A person at the roof closes it.
6. **A jog stops at the first magnet the current thresholds see**, as constitution I asks of every
   move from an unknown position, and never raises the wrong-magnet fault (the polarity may be what
   is being taught).
7. **The position becomes known only through the confirmed thresholds**, not from the person's
   mark alone (constitution I: position is known only at a magnet). If the roof is on a taught end
   when the proposal is confirmed, it reads as that end at once.
8. **Threshold at the halfway point** between the part-way reading and the end's, inside the same
   window the run uses (clear of the part-way readings, read at rest at the end). The halfway point
   is the hand rule of the calibration page.
9. **Marks in memory only**, kept until confirmed, discarded or the device restarts; a new mark
   replaces the old one for the same place.
10. **The confirm saves on the device** (unlike the run's Accept, which the page saves): the route
    the step asks for, and only the three Hall values change. The values must match the device's
    current proposal.
11. **Marks are allowed while a fault is latched**: reading the sensor moves nothing, and the Hall
    rail check still refuses a broken sensor.
12. **"Authenticated" is the device's login**: with password protection off, as for every roof
    command, anyone on the trusted LAN can jog (constitution VIII), still needing the attended
    confirmation and the command header.
13. **Jogs show as calibration moves in the diagnostics log** (spec 017), since they share the
    calibration leg's machinery.
14. **The wizard page moves nothing**: like the Calibration run page, it explains and links.
    The wizard gets a twelfth page; a saved `setup.step` from before this release resumes one page
    earlier, which only repeats a page. Spec 018's redesigned wizard will need the page re-homed
    into its screen list when whichever of the two merges second is rebased.
15. **Constitution 2.0.0 → 2.0.1 (PATCH)**: the wording of Principle II is clarified to match the
    roadmap's design decision and the code since spec 004 (switches optional, a setting says which
    are fitted). No rule changes, so it is not a relaxation.
16. **The UI mockup review is pending**: the owner reviews the card in the demo before merging.

## Out of scope

- Proposing the travel, speed or margin (spec 016's run does).
- Jogging from Alpaca, MQTT or Home Assistant; a jog in bad weather.
- Jogging above the slow duty; any jog without a person holding the button.
- Changing the park rule's behaviour.
