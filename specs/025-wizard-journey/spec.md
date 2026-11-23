# Feature Specification: Setup Wizard Journey

**Feature Branch**: `spec/025-wizard-journey`

**Created**: 2026-11-23

**Status**: Draft

## Context

Specs 015 and 018 built the setup wizard: a dialog over the dashboard that asks one question per
screen. Specs 016, 019, 020, 022 and 023 each added screens to it. Walked end to end on a real
bench box, the result no longer reads as one journey for a new install:

- **It sends people out.** The Teach and calibration screens only link to the Calibrate page;
  the alert screen's MQTT channel offers "Turn on MQTT", which opens Settings. Coming back, the
  wizard reopens at the start of its part with the unsaved answers gone, and on a device whose
  setup is already done (a re-run from System) at the very first screen.
- **It asks for numbers instead of running the motor.** The Hall screen asks for thresholds, the
  motion screen for travel, speed and top duty, while the screens that would measure them (spec
  019's jog and teach, spec 016's calibration run) are elsewhere.
- **It asks twice.** SQMeter's address and login are asked on the SQMeter screen and again on the
  weather address screen; the site and time screens ask what SQMeter already supplied.
- **Answers come undone.** Choosing "No password" is reversed on the next visit to that screen:
  the screen turns protection back on by itself whenever the wizard is opened again, and a
  re-run from System always opens at the first screen. Other answers (the Hall "not fitted yet",
  the alert channel picked) are kept only in the open page and are lost on a reload.
- **The alert step is narrower than Settings.** One channel only, no "none", and a channel's
  fields are on the next screen, so choosing Pushover fails Continue with "Please fix 2
  validation errors" and no field to fix.
- **The order is the history of the specs**, not the order an installer works in.

This spec reworks the wizard into one journey with a test-box (bench) branch and a
real-observatory branch, using the screens and components that already exist. It changes no
setting, no API and no safety rule. Budgets are tight (the UI's LittleFS image is at 74% of its 75%
limit), so the rework removes screens and text rather than adding them.

## Journey map

### Before (spec 023)

| Part (`setup.step`) | Screens |
|---|---|
| 0 Device | name, password (auto-chosen "Set a password") |
| 1 Site and time | SQMeter address + login + copy, location, time zone and NTP |
| 2 Hardware | board (Custom leaves for Pins), network mode, test box, supply vs meter, current sense, park switches, wiring check (links out to Calibrate) |
| 3 Hall sensor | fitted?, thresholds typed |
| 4 Teach | link out to Calibrate |
| 5 Calibration run | link out to Calibrate |
| 6 Roof motion | travel, speed, top duty typed |
| 7 Weather | source (web, MQTT, both), then SQMeter's address and login again, with Test |
| 8 N.I.N.A. | address with Test; Alpaca on or off |
| 9 Alerts | one channel; then its fields on the next screen ("Turn on MQTT" leaves) |
| 10 Daylight | do nothing, alert, alert and close |
| 11 Review | checks with Change |

Up to 25 screens; 4 leave the wizard; 2 ask SQMeter's address; 6 ask for numbers the motor
could measure.

### After (this spec)

| Part (`setup.step`) | Screens | Test box branch | Real observatory branch |
|---|---|---|---|
| 0 Network | name; network mode (boards with Ethernet) | same | same |
| 1 Board | board, with Custom pins inline | same | same |
| 2 Wiring | wiring check, with current sense fitted | same | same |
| 3 Test box | telescope or test box | test box | telescope |
| 4 SQMeter | SQMeter (address, login, Test, copy, weather from SQMeter); site and time only when not from SQMeter; daylight | weather bypassed, SQMeter for site data only | weather from SQMeter |
| 5 Mount park | park switches; N.I.N.A. with Test and Alpaca | N.I.N.A. skipped (park bypassed) | N.I.N.A. asked |
| 6 Direction | hold to move: did it open? | gates bypassed, named | park and weather gates |
| 7 Teach | mark closed, part-way, open; confirm | same | same |
| 8 Calibration | the run, its proposal, Accept; "Enter manually instead" opens the typed values | same | same |
| 9 Alerts | several channels or none, each channel's fields inline (MQTT broker included) | same | same |
| 10 Password | set one or not | same | same |
| 11 Review | checks with Change | same | same |

At most 17 screens on the real-observatory branch (fewer when SQMeter supplies the site and time),
16 on the test box branch; none leaves the wizard except Custom pins' Advanced analog section and
Settings-only fixes, which save first and come back to the same part.

## Clarifications

### Session 2026-11-23

- Q: Where does the wizard remember how far it got: per screen or per part? → A: Per part, as
  today (`setup.step`, 0 to 11): the twelve parts are redrawn so each starts where an installer
  would pick up again. No device setting changes.
- Q: Does the motor part replace the Calibrate page? → A: No. The page stays for re-teaching and
  re-running later; the wizard shows the same components itself and never links to the page.
- Q: How is "Hall sensor not fitted yet" answered now? → A: It isn't asked. The direction screen
  shows the live Hall reading, the wiring check flags a disconnected sensor, and a person without a
  motor or sensor yet takes "Enter manually instead" or skips the motor part.
- Q: Does the attended confirmation carry from one motor screen to the next? → A: No. Each
  screen that can move the roof has its own "I am at the roof" confirmation, as the existing
  components do; it is never saved and never carries over a reload.
- Q: Where does the supply-against-a-meter comparison go? → A: The wiring check reads the supply,
  and the Pins page's supply calibration (spec 023) corrects it against a meter; the wizard drops
  its own comparison screen.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The motor is set up inside the wizard (Priority: P1)

An installer at the roof, motor connected, reaches the motor part. They confirm they are at the
roof, hold Open and see whether the roof opens; hold it to each end and mark closed, part-way and
open; confirm the proposed Hall settings; then start the calibration run, answer its "is it open?"
question and accept the measured travel, speed and thresholds. They never leave the wizard, and
Stop is always on screen while the roof moves.

**Why this priority**: The motor part is what the owner found missing: the wizard asked for
numbers the roof can measure and sent them elsewhere to measure them.

**Independent Test**: In the demo, restart the wizard, go to the direction screen, and finish the
motor part with the simulated roof; the device's settings hold the taught thresholds and the
calibration's travel and speed.

**Acceptance Scenarios**:

1. **Given** the direction screen, **When** the person has not confirmed they are at the roof,
   **Then** the hold buttons are unavailable with that reason shown.
2. **Given** the park or the weather not confirmed (and no test box bypass), **When** the
   direction, teach or calibration screen shows, **Then** the failing check is named and no move
   can start.
3. **Given** a test box with bypasses in effect, **When** a motor screen shows, **Then** it says
   which checks are bypassed and that faults, the supply and the time limits still stop the roof.
4. **Given** the person answered "No, it closed" on the direction screen, **Then** the screen says
   to swap the motor's two wires and Continue stays unavailable.
5. **Given** the teach proposal confirmed, **Then** the device's Hall settings are the proposed
   ones and the wizard shows them saved.
6. **Given** a finished calibration run, **When** the person accepts, **Then** the proposal is
   saved through the settings save and the wizard shows it saved.
7. **Given** no motor yet, **When** the person chooses "Enter manually instead", **Then** the typed
   Hall, travel, speed and top duty fields show, and Continue goes on to the alerts.

---

### User Story 2 - The wizard never sends you out (Priority: P1)

Every setting a screen needs is edited on that screen: MQTT's broker under the MQTT alert channel,
Custom pins under the board choice. Where something can only be done elsewhere (a Settings-only
fix, the Pins page's Advanced analog section), the wizard saves the screen first, records the part,
and opens the wizard there again when the person comes back.

**Why this priority**: Leaving and coming back lost answers and, on a re-run, restarted the
wizard.

**Independent Test**: Walk every screen of both branches in the demo; no control routes away from
the wizard except the documented save-and-leave ones, and after each of those, opening the wizard
again shows the same part with the answers kept.

**Acceptance Scenarios**:

1. **Given** the alerts screen with MQTT off on the device, **When** MQTT is ticked, **Then** MQTT
   is turned on and its broker fields are on the same screen.
2. **Given** Custom chosen on the board screen, **Then** the pin fields and the device's pin issues
   show on that screen.
3. **Given** a save-and-leave link, **When** it is used and the person returns to the wizard,
   **Then** it opens at the same part with the screen's answers saved.

---

### User Story 3 - Progress is reliable across reloads (Priority: P1)

A person who reloads the page, closes the browser or comes back later finds the wizard where they
left it, with their answers as they left them. "No password" stays no password. A re-run from
System starts at the beginning once, and a reload during the re-run resumes where it was.

**Why this priority**: The reported password bug, and the same kind of bug in other screens.

**Independent Test**: Choose "No password", continue, reload; go back to the password screen: "No
password" is still chosen and Continue is available. Repeat the reload at every part.

**Acceptance Scenarios**:

1. **Given** "No password" chosen and saved, **When** the wizard is opened again by any path,
   **Then** the password screen shows "No password" chosen and protection off.
2. **Given** the wizard opened from System ("Run setup wizard"), **When** the person continues to a
   later part and reloads, **Then** the wizard opens at that part, not at the first screen.
3. **Given** a part skipped with "Skip for now", **When** the page is reloaded, **Then** the wizard
   opens at the next part, not the skipped one.
4. **Given** a screen whose answer lives only in the page, **Then** there is no such screen: every
   answer is a saved setting or derived from one.

---

### User Story 4 - Alerts as in Settings (Priority: P2)

On one screen the person ticks any number of channels (ntfy, Pushover, a webhook, MQTT) or "No
alerts for now", unticks any of them, and fills each ticked channel's fields right there, prefilled
from SQMeter where they were copied. A validation error names the field and sits next to it.

**Why this priority**: The single choice was narrower than Settings, and Pushover failed with no
field to fix.

**Independent Test**: Tick Pushover and ntfy, leave Pushover's keys empty, press Continue: each
missing key shows its error under its field, and the screen's note names them.

**Acceptance Scenarios**:

1. **Given** ntfy and Pushover ticked, **Then** both channels' fields show and both are saved on.
2. **Given** "No alerts for now" ticked, **Then** every channel is unticked and saved off, and the
   review shows that no channel is set.
3. **Given** a ticked channel is unticked, **Then** it is saved off and its fields hide.
4. **Given** a refused Continue for missing fields, **Then** each error is under its field and the
   note says which fields to fix.

---

### User Story 5 - Ask once, in an installer's order (Priority: P2)

The journey runs network, board and wiring, test box or not, SQMeter, mount park, motor, alerts,
password, review. SQMeter's address and login are asked once. A question SQMeter answered (the
location, the time zone) shows as done with Edit instead of being asked again.

**Independent Test**: Walk the real-observatory branch in the demo after copying from SQMeter: the
site and time screen doesn't show; the SQMeter screen lists them as from SQMeter with Edit.

**Acceptance Scenarios**:

1. **Given** the location and time follow SQMeter, **Then** no separate site and time screen shows
   and the SQMeter screen lists both as done, each with Edit, which opens their fields.
2. **Given** no SQMeter address, **Then** the site and time screen shows.
3. **Given** the test box branch with the weather bypassed, **Then** the SQMeter screen keeps the
   address for site data and says the weather check is bypassed.
4. **Given** the rain contact removed by spec 023, **Then** no screen, review row or text mentions
   a local rain input.

### Edge Cases

- A move started on a motor screen when the person presses Back, Close or Escape: the hold ends
  as the jog's own release does (the screen closes and releases); a calibration run keeps going
  under the device's own limits, the Stop bar stays in the dialog (and on every page) until it
  stops, and the calibration screen shows the run again when it opens.
- A reload during a jog: the device stops it by itself when the keep-alives stop (spec 019).
- A reload during a calibration run: the calibration screen shows the run still active with Stop
  and its result afterwards (the run's status comes from the device).
- The device refuses a teach confirmation or a calibration accept (the roof moved, settings changed
  on the device): the reason shows on the screen, the wizard stays.
- A board change needs a restart before the wiring check and the motor screens can work: those
  screens say so and offer the restart (as today).
- Spec 024 merged first: its park question replaces the park switches screen in the mount park
  part, with the same save path.

## Requirements *(mandatory)*

### Functional Requirements

**Journey and progress**

- **FR-001**: The wizard MUST ask its questions in twelve parts, in this order: network, board,
  wiring, test box, SQMeter, mount park, direction, teach, calibration, alerts, password, review.
  `setup.step` (0 to 11) records the part, as before; no setting changes.
- **FR-002**: Continue and Skip for now MUST both record the part the next screen belongs to.
  Skip for now saves nothing else.
- **FR-003**: Opening the wizard without an explicit start (a reload, the dashboard banner, a
  return from a save-and-leave link) MUST resume at the recorded part, whatever the setup state.
  An explicit start (System's "Run setup wizard", a `?screen=` link) applies once: the address is
  then replaced, so a reload resumes instead.
- **FR-004**: No screen MAY change a setting by itself when it opens. A choice that matches the
  factory value (no password, no park switches) MUST be shown as chosen whenever the person has
  passed that screen before (the resume point is beyond it) and stay as saved.
- **FR-005**: No setting's answer MAY live only in the open page. The Hall "fitted" question and
  the single-channel alert pick are removed; the alert channels are read from the settings. The
  direction check's answer is a confirmation, like "I am at the roof": asked again on a screen not
  yet passed, never saved.

**Never sends you out**

- **FR-006**: No wizard control MAY navigate away from the wizard, except: the Pins page for the
  Advanced analog section, and a dependency fix that only Settings can make. Each of those MUST
  save the screen (or record the part when nothing changed) before leaving, and the wizard MUST
  open at that part again on return.
- **FR-007**: Dependency fixes the wizard can make itself MUST be made in place: turning on MQTT,
  alerts or Alpaca; going to the SQMeter or site screen for an address, a location or the clock.
- **FR-008**: Custom pins MUST be edited on the board screen, with the device's pin issues there.
- **FR-009**: The wiring check MUST NOT link to the Calibrate page from the wizard.

**Motor part**

- **FR-010**: The direction screen MUST offer spec 019's hold-to-move jog (its checks, the
  attended confirmation, hold buttons, Stop and the live Hall reading) and ask whether holding Open
  opened the roof. "No" says to swap the motor's two wires; Continue needs "Yes".
- **FR-011**: The teach screen MUST offer spec 019's teach (jog, mark closed, part-way and open,
  the proposal, confirm or discard), the same component as the Calibrate page.
- **FR-012**: The calibration screen MUST offer spec 016's run (its checks, attended confirmation,
  where the roof is, the measured travel, the optional top-duty pass, the run with Stop and its
  open-end question, the result and proposal with Accept or Discard), the same components as the
  Calibrate page, saving an accepted proposal through the settings save.
- **FR-013**: Every gate of those components MUST stay: the attended confirmation, the park and
  weather checks or a test box bypass, the fault, sequence, supply and Hall checks, and the device's
  own checks when a move starts. Nothing moves without a person holding or starting it.
- **FR-014**: While a test box bypass is in effect, each motor screen MUST say which checks are
  bypassed and that faults, the supply and the time limits still stop the roof.
- **FR-015**: Each motor screen MUST offer "Enter manually instead", which opens one screen with
  the Hall end and thresholds (with the live reading), travel, speed and top duty, and "Use
  first-run values" while the motion values are the factory ones.
- **FR-016**: A held jog MUST end when its screen closes (Back, Continue, Close). A calibration run
  keeps going under the device's own limits when its screen closes; the Stop bar stays in the
  dialog while the roof moves, and the calibration screen shows the run (with Stop) or its result
  whenever it opens again.

**Ask once**

- **FR-017**: SQMeter's address and login MUST be asked once, on the SQMeter screen, with its Test
  and the copy of its site settings, and whether the weather verdict comes from SQMeter (web
  address, MQTT topic). The separate weather source and weather address screens are removed.
- **FR-018**: The site and time screen MUST show only when the location or the time does not
  follow SQMeter. When both do, the SQMeter screen lists them as done, each with Edit, which opens
  the site and time screen.
- **FR-019**: On the test box branch with the weather bypassed, the SQMeter screen MUST NOT turn
  on SQMeter's weather source and MUST say the weather check is bypassed.
- **FR-020**: N.I.N.A.'s address, its Test and the Alpaca choice MUST be one screen; it is skipped
  while the park is bypassed (test box), as before.

**Alerts**

- **FR-021**: The alerts screen MUST be a multiple choice of ntfy, Pushover, webhook and MQTT, plus
  "No alerts for now", which clears the others; any choice can be unticked.
- **FR-022**: Each ticked channel's fields MUST show on the same screen, with Send test once saved;
  ticking MQTT while MQTT is off turns MQTT on and shows its broker fields there (unticking puts it
  back as the device had it, unless the weather still reads SQMeter over MQTT).
- **FR-023**: A refused save MUST show each field's error next to the field, and the screen's note
  MUST name the fields to fix rather than only count them.

**Password and review**

- **FR-024**: The password screen MUST start with nothing chosen on a screen not yet passed, "Set a
  password" marked recommended, and keep whatever was saved afterwards.
- **FR-025**: The review MUST list the same essentials, with Change opening the part that sets each
  one; the Hall row MUST come from the live reading.

**Design system and size**

- **FR-026**: The alerts' multiple choice MUST be the design system's choice card group in its
  checkbox form (DS-12 extended), notes and errors MUST be `Note`s, and no screen MAY add its own
  coloured text.
- **FR-027**: The UI's stored size MUST stay within SIZE-01 and SIZE-02; removed screens' strings
  are removed.

### Key Entities

- **Part**: one of the twelve resume points (`setup.step`), each a short run of screens.
- **Screen**: one question, shown or skipped from the settings (and the device's status), never
  from page-only state.

## Design *(mandatory for UI changes)*

- DS-11 (the modal, its progress bar, Back, Close, the footer, the Stop bar), DS-12 (choice cards;
  extended to a checkbox group for questions with several answers), DS-13 (the mascot), DS-20 to
  DS-29 (copy), DS-04 and DS-06 (notes and a task's progress in its own place).
- The motor screens reuse the Calibrate page's Teach card, start form, run and result without their
  card frames, inside the dialog. Stop stays in the dialog's Stop bar and on the run itself.
- Screenshots of the new screens at desktop and phone width replace the owner mockup review (see
  assumptions).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A person can finish the whole wizard on the real-observatory branch in the demo
  without leaving it: zero navigations away from `/wizard` in the end-to-end test.
- **SC-002**: At most 17 screens on the real-observatory branch and 16 on the test box branch
  (25 before).
- **SC-003**: SQMeter's address is asked on exactly one screen.
- **SC-004**: A reload at any part resumes at that part with every saved answer shown, in 100% of
  the parts tested, including after "No password".
- **SC-005**: The demo's simulated roof is jogged, taught and calibrated from the wizard, and the
  device's settings afterwards hold the taught and measured values.
- **SC-006**: The UI's LittleFS image stays within its 75% limit and its gzipped code grows by no
  more than 2 KB.

## Assumptions decided for the owner

- The wizard's order follows the installer's journey above; the password moves near the end, after
  the motor part. Until then the device is open to the LAN, as the "No password" warning says;
  Settings can set one at any time.
- "Set a password" is no longer chosen for the person on a new device: nothing is chosen until
  they answer, with "Set a password" marked recommended. A setting that changes by itself when a
  screen opens was the cause of the password bug.
- Resume stays per part (`setup.step`, 0 to 11) with the parts redrawn; an upgraded device in the
  middle of the old wizard resumes at the part with the same number, which is only a resume point.
- The Hall "fitted?" question and the supply-against-a-meter screen are removed; the wiring check
  and the Pins page's supply calibration cover them.
- The weather Test on the old weather address screen is replaced by SQMeter's own Test on the
  SQMeter screen (one read of SQMeter through the device); the review still shows the device's
  verdict.
- Each motor screen asks for its own attended confirmation; none is saved or carried over.
- The owner's mockup review is replaced by the screenshots in the pull request.
- Spec 024's park question, if merged first, replaces the park switches screen in part 5 unchanged.

## Design-system follow-ups carried in this change

Found while reviewing the test box banner (merged just before this spec), and fixed here because
the motor screens reuse the same parts:

- **FR-028**: A failing calibration or jog check's reason sits under its row as a muted `Note`;
  the row's pill already says "Not yet". No bare red text line.
- **FR-029**: The test box page's "Turn it off before a telescope goes under the roof." is a muted
  `Note`; the page's banner and the Turn off button carry the urgency.
- **FR-030**: The dashboard's Status card never reads "All good" while a test box bypass is in
  effect: a "Test box: Bypassed" row links to the test box page (constitution 4.1.0: a bypassed
  check is never shown as passing).

## Out of scope

- Any setting, API, firmware or safety rule change.
- The Calibrate page itself (it stays as it is for later re-teaching).
- New alert channels or settings the wizard didn't ask before (Home Assistant, alert levels).
