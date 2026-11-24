# Feature Specification: A Cohesive Setup Journey

**Feature Branch**: `spec/026-cohesive-journey`

**Created**: 2026-11-24

**Status**: Draft

## Context

The first real roll-off roof set up with RoofRoof showed that the pieces work but the journey does
not. The audit in [journey-audit.md](journey-audit.md) walks the demo as a new owner and lists 40
confusing moments. Five problems stand out:

1. **Why won't the roof move?** Refusals and holds are scattered rows ("Automatic close idle: Not
   yet", "Not ready: ...") with no meaning, no "clears when" and no fix. A person's Stop during an
   automatic close stands it down for up to the re-arm spell (10 minutes by default); jog, teach and
   the calibration run are refused meanwhile, and nothing says so.
2. **Move time limits.** A move's hard time limit comes from travel and speed plus the margin.
   With the defaults (1850 mm at 38 mm/s) a move is allowed about 66 s; a slower roof faults "No
   target magnet after 65.9 s" on every move, and nothing says where the limit comes from or that the
   calibration run measures it.
3. **Setup is spread out** over Settings, Calibrate, Pins, the test box page, the wizard and the
   dashboard, with no order and no record of what is done.
4. **Following SQMeter overwrote typed values.** A login typed while the MQTT group followed SQMeter
   was replaced at the next sync, with no notice.
5. **Test box.** Turning the weather bypass on took two saves on two pages: after the first (the
   SQMeter source off) the weather read "no weather source", unsafe, and the automatic close started.

## Clarifications

### Session 2026-11-24

Each answer takes the safest option the constitution allows; they are also listed under
"Assumptions decided for the owner".

- Q: Where does the explanation of a refusal live? → A: One pure module (`moveBlockers`) and one
  component draw every refusal or hold on the dashboard, Calibrate and the wizard's motor screens:
  what stops the move (with the device's value), what it means ("?"), when it clears, and one fix.
- Q: May a person end the automatic close's stand-down early? → A: Yes, **Release hold**, only
  for a person at the roof (confirmed on the page and in the request), only while the weather check
  reads safe and daylight doesn't want the roof closed (the device checks again at its next step),
  on the web page only. It never moves the roof; it arms the automatic close again, exactly as the
  end of the re-arm spell does. A can't-move stand-down (fault, unknown position) is not released:
  it is cleared by fixing its cause.
- Q: Where is "clears at 00:25" computed? → A: The device reports the stand-down's seconds left
  (`roof.closeRearmSeconds`, null while it isn't counting down); the page adds them to its clock.
- Q: How is "measured" known? → A: A new setting `roof.measured` (bits: travel, speed, margin, the
  Hall end and thresholds, the direction) is set by an accepted calibration run, a confirmed teach
  and the wizard's direction answer, and cleared per value by a save that changes the value without
  sending `measured`.
- Q: What does typing in a group that follows SQMeter do? → A: Sets that group here (the page sends
  `follow: false` with the edit and shows "Set here, not from SQMeter" with **Follow SQMeter
  again**). A secret the device asks for (SQMeter has one RoofRoof lacks) only fills the gap. The
  device applies the rule to every save: changed values without a follow flag make the group local;
  a save that says it follows (a copy from SQMeter) keeps it following.
- Q: Test box: atomic save or bypass first? → A: One save. Turning **No weather station** on while
  an SQMeter source is on turns the source off in the same save, with a note saying so. With every
  weather source off and no bypass, Settings warns that the automatic close will close the roof.
- Q: LittleFS is at its limit; how is the new UI paid for? → A: The built page leaves out the
  English-only `device.*` catalogue (English device text shown as sent is the same words); the page
  ends smaller than before.

### Session 2026-11-24 (owner decisions)

The owner answered the questions under "Left for the owner":

- Q: Should a person's Open end the hold? → A: **Yes.** An Open by a person or a client that starts
  the roof moving ends a person's stand-down at once, the same transition as **Release hold** and
  the end of the re-arm spell (spec 005, constitution 6.0.0). A halted close is not ended by an
  Open. The page no longer says the close "won't close by itself" without an end: the hold reads
  "Automatic close on hold until the roof opens or closes".
- Q: Should **No telescope under this roof** (the test box page) and the wizard's **No telescope
  yet** clear the N.I.N.A. address in the same save? → A: **Yes** (spec 022's record has the
  details). The wizard's SQMeter weather source is unchanged by this decision.
- Q: Should **Release hold** also be offered to Home Assistant? → A: **No.** It stays on the web
  page only: it is for a person at the roof who can see it.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Why can't the roof move? (Priority: P1)

A new owner presses Open, or holds Open to jog, and it is refused or held. They see each reason in
plain words, what it means, when it clears, and the one thing that fixes it, in the same words on
every page.

**Independent Test**: in the demo, create each refusal (weather unsafe, mount not parked, held by a
person, a timeout fault, low supply, the Hall at its rail, test box, SQMeter offline or locked) and
check the dashboard and Calibrate list it with its fix.

**Acceptance Scenarios**:

1. **Given** the weather unsafe, **When** the owner opens Calibrate, **Then** one list says "Weather
   check: ..." with "Clears once SQMeter reports safe." and **Weather check settings**.
2. **Given** a person stopped an automatic close and the weather is safe again, **When** they look at
   the dashboard or Calibrate, **Then** it says the automatic close is on hold after a Stop, that it
   clears at a clock time if the weather stays safe or once the roof is closed, and offers **Release
   hold**; releasing asks whether they are at the roof, and afterwards jog and the run are available.
3. **Given** the weather unsafe while held, **Then** no release is offered and the hold says it
   clears after the safe spell; the device refuses a release request (409).
4. **Given** a move that ran out of time, **Then** the fault's "?" says the limit comes from travel,
   speed and the margin and to run the calibration run, with **Clear fault** beside it on Calibrate.
5. **Given** nothing blocks a move, **Then** the list says "Nothing stops a move now." and names any
   check a test box bypass stands in for.

### User Story 2 - One place for setup progress (Priority: P1)

The owner sees the roof's setup stages in order, what is done, and a link to each; before the
calibration run they are told the move time limits are still defaults.

**Independent Test**: a fresh demo shows the Roof setup card with seven stages and "Move time limits
are still defaults (about 66 s a move)"; Settings, Roof starts with the same list.

**Acceptance Scenarios**:

1. **Given** a new device, **Then** the dashboard shows Roof setup with each stage's state and link.
2. **Given** an accepted calibration run, **Then** the calibration stage is done and the note under
   travel and speed says they were measured on the roof.
3. **Given** travel typed by hand, **Then** the stage says "Not measured" and suggests a run.
4. **Given** a test box bypass, **Then** its stage says "Bypassed", never done.
5. **Given** every stage done, **Then** the dashboard card goes.

### User Story 3 - Settings in setup order, limits beside travel and speed (Priority: P2)

Settings, Roof runs in setup order; travel, speed and the margin sit with the move time limits they
make, as the device works them out, with who set them and the calibration run's link.

**Acceptance Scenarios**:

1. **Given** the defaults, **Then** Travel and time limits shows "Move time limit 65.9 s" and "From
   an unknown position" and says the values are still defaults, with a link to the run.
2. **Given** the owner types a slower speed, **Then** the limits change as they type.

### User Story 4 - Following SQMeter never overwrites what was typed (Priority: P1)

**Acceptance Scenarios**:

1. **Given** the MQTT group follows SQMeter, **When** the owner types their own user name and saves,
   **Then** the group shows "Set here, not from SQMeter" with **Follow SQMeter again**, and later
   syncs leave the typed value.
2. **Given** SQMeter has a password RoofRoof lacks and asks for it, **When** the owner types it,
   **Then** the group keeps following.
3. **Given** a script saves a followed group's value without a follow flag, **Then** the device makes
   that group local.

### User Story 5 - Test box without a surprise close (Priority: P1)

**Acceptance Scenarios**:

1. **Given** an open roof on a test box with the SQMeter source on, **When** the owner turns **No
   weather station** on and saves, **Then** the source goes off in the same save and no automatic
   close starts.
2. **Given** every weather source off and no bypass, **Then** Settings warns that the automatic close
   will close the roof, with a link to the test box page.

### Edge Cases

- Live data lost: the only blocker is "No live data", whatever the last reading said.
- A fault and a can't-move automatic close: only the fault is listed.
- A stand-down while daylight wants the roof closed: no release, no countdown.
- A new Stop on the same step as a release: the Stop wins.
- Older firmware without `closeRearmSeconds` or `measured`: the hold says "after the safe spell";
  stages read from typed values only.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Every refusal or hold of Open, Close, jog, teach or the calibration run MUST be listed
  by one shared module and component, in the device's order of checks, each with what stops it (the
  device's value), what it means, when it clears and at most one fix.
- **FR-002**: The dashboard, Calibrate (once, above the teach card and the run) and the wizard's
  direction, teach and calibration screens MUST use it. A greyed button MUST point at it.
- **FR-003**: A stand-down (`heldByPerson`, `halted`) MUST say when it clears: a clock time from the
  device's `closeRearmSeconds` while counting, otherwise after the re-arm spell, or once the roof is
  closed.
- **FR-004**: **Release hold** MUST be offered only while the device would take it (stand-down, the
  weather safe, no daylight close), MUST ask for an attended confirmation, MUST send
  `POST /api/roof/release-hold` with `attended: true`, and the device MUST refuse it otherwise. The
  release MUST NOT move the roof and MUST NOT be offered over Alpaca, MQTT or Home Assistant.
- **FR-005**: The device MUST report `roof.closeRearmSeconds` in the status document.
- **FR-006**: The roof's setup stages MUST be shown, in order, with their state and link, on the
  dashboard until all are done and at the top of Settings, Roof.
- **FR-007**: Before travel and speed are measured, the page MUST say the move time limits are still
  defaults (or typed), with the time a move is allowed and a link to the calibration run.
- **FR-008**: Settings, Roof MUST show the move time limits beside travel and speed, computed as the
  device does, and who set travel and speed (measured, default, typed).
- **FR-009**: The device MUST keep `roof.measured` as specified in the clarifications.
- **FR-010**: A timeout fault's explanation MUST say where the limit comes from and to run the
  calibration run.
- **FR-011**: Typing in a field of a group that follows SQMeter MUST set that group here in the same
  edit, with a visible note and **Follow SQMeter again**, except a secret the device asks for.
- **FR-012**: A settings save MUST send the follow flag of every SQMeter group whose values it
  changes; the device MUST make a group local when a save changes its values without a flag.
- **FR-013**: Turning the test box's weather bypass on beside an SQMeter source MUST turn the source
  off in the same save, with a note; Settings MUST warn when every weather source is off without the
  bypass.
- **FR-014**: The built page MUST NOT grow LittleFS use; it leaves the English-only device catalogue
  out of the build.
- **FR-015**: Terms MUST be consistent: weather check, park check, open and closed ends, slow and
  top duty, test box, follow SQMeter.

### Key Entities

- **Blocker**: id, tone, title, meaning, clears, fix (a route, Clear fault or Release hold).
- **Setup stage**: board, weather, park, direction, ends, calibrated, alerts; done, to do, not
  measured or bypassed; a route.
- **roof.measured**: bits 1 travel, 2 speed, 4 margin, 8 Hall end and thresholds, 16 direction.
- **roof.closeRearmSeconds**: seconds or null.

## Design *(mandatory for UI changes)*

Only design-system parts (constitution VII): `Note` (one line per state, the fix as its action),
`InfoTip` for meanings (DS-04, DS-23), `Pill` for stage states (DS-02, DS-21), `Card` and
`SettingsCard`, `ReadingRow`, `Button` (link variant for stage links). No new colours or tokens;
one CSS rule (`.move-blocker`) replaces the calibration checklist's. Copy follows DS-20 to DS-28
(`tools/ui/copy_check.py`).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every refusal the demo can produce shows its explanation and fix on Calibrate (Playwright
  `journey.spec.ts`).
- **SC-002**: A stand-down after a Stop can be released at the roof while safe in two presses.
- **SC-003**: A new owner can see from one card what is set up and what is not (7 stages).
- **SC-004**: Turning the test box weather bypass on with the roof open starts no automatic close.
- **SC-005**: A typed MQTT login survives the next sync.
- **SC-006**: The built UI does not grow LittleFS use: it is 2 KB gzipped smaller than main, within the
  75% limit.

## Assumptions decided for the owner

1. **Release hold** is allowed only attended, only while the weather check reads safe and daylight
   doesn't want the roof closed, for `heldByPerson` and `halted` only, on the web page only. It only
   arms the automatic close again (the same transition as the end of the re-arm spell); it never
   moves the roof. A person's Stop still stands the sequence down at once, so nobody can be made to
   fight a close in real rain: while it is unsafe the hold cannot be released. Constitution Principle
   III is unchanged (no gate is weakened). Its bullet "Alerts, MQTT and the web UI report the verdict.
   They MUST never be able to change it, or the motion decisions taken from it" doesn't apply: the
   stand-down comes from a person's Stop, not from the verdict, and the release can only make the
   safety close possible again sooner, never stop it. "Attended" is the person's own statement; with
   login off any LAN client can send it (Principle VIII's trusted LAN, as for Open and Close). The
   decision is recorded here and in the user guide.
2. Jog, teach and the calibration run still wait for the automatic close to be idle (unchanged); the
   hold is explained and releasable instead of the gate being relaxed.
3. `roof.measured` starts at 0 on an upgrade: an upgraded roof whose travel and speed were measured
   before shows "Not measured" until the next calibration run (honest, and one run fixes it).
4. A script's save that changes a followed group without a follow flag makes the group local
   (previously the device kept the synced values). A person's value is never replaced silently.
5. A secret SQMeter has and RoofRoof is asking for, typed by the person, keeps the group following.
6. The test box's weather bypass is not offered in one click while a configured SQMeter source
   reports unsafe (a bypass never hides a reading); turning it off again before saving puts the saved
   sources back.
7. Every follow-group value counts as typed, the on/off switches too (MQTT, ntfy, Pushover): turning
   one off while the group follows sets the group here, so the next sync can't turn it back on. The
   same rule applies on Settings pages and in the wizard. A Pushover sound is not part of the group.
8. The device's English text is shown as sent; the `device.*` catalogue stays in `en.json` for the
   checks and a future language, and is left out of the build.
9. The wizard's bench branch still keeps a N.I.N.A. address and an SQMeter source when they are set
   (spec 022's choice); see "Left for the owner".
10. The dashboard's Open and Close stay available while the automatic close is held (the firmware's
   rule, unchanged).

## Left for the owner

Answered on 2026-11-24 (see the clarifications above); kept here as they were asked.

- **Opening the roof while the automatic close is on hold keeps the hold.** A person who stops an
  automatic close, waits for the weather to clear and opens the roof again leaves the sequence stood
  down: if rain returns within the re-arm spell, the roof is not closed by itself (spec 005's rule,
  unchanged here). The page now says so plainly ("Automatic close on hold: it won't close by
  itself"). Should a person's Open end the hold, as **Release hold** does? That changes the close
  sequence, so it needs the owner's decision (it would only arm the safety close sooner).
- Should the test box page's **No telescope under this roof** clear the N.I.N.A. address in the same
  save (as **No weather station** now turns the SQMeter source off), instead of pointing at Mount park?

- Should the wizard's "No telescope yet" clear the N.I.N.A. address and turn the SQMeter weather
  source off in the same save, as the test box page now does for the weather bypass?
- Should **Release hold** also be offered to Home Assistant (as a button entity)?

## Out of scope

- The calibration run started from the open end (fix/calibration-start-open), Hall filtering
  (fix/hall-noise-and-alert-pause).
- Changing the re-arm rule, the park rule or any motion gate.
- A navigation entry for Calibrate, Pins and Test box (reached from the stages instead).
