# Feature Specification: Test Box Bypass

**Feature Branch**: `spec/022-test-box-bypass`

**Created**: 2026-11-23

**Status**: Draft

## Context

A RoofRoof is often built and tried out on a desk long before it goes on a roof: a real board, a
real motor and a Hall sensor, but no telescope under it, no roof and no weather station yet. On such
a box nothing that moves can be tried. Jog, teach, the calibration run, Open and Close are all
refused, because:

- the park check needs N.I.N.A. to report a parked mount ("Mount not parked: N.I.N.A. says mount
  not at park"), and there is no mount;
- the weather check needs a weather source ("No weather source: SQMeter HTTP and MQTT are both
  off"), and there is no station;
- in daylight with the daylight failsafe on "alert and close", opening is refused and an open roof
  is closed.

The owner can't satisfy these on a bench. This spec adds a **test box mode**: per-check bypasses
the owner sets explicitly, that are shown loudly everywhere, that expire on their own, and that
never touch a fault. It deliberately relaxes constitution Principles II and III for that case
(constitution 3.0.0, MAJOR).

The existing bench build (`ROOFROOF_BENCH`: a simulated roof, the motor's pins held low, USB only)
is unchanged. Test box mode is for the real firmware on a real board with a real motor.

## Clarifications

### Session 2026-11-23

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: What does the park bypass mean? → A: A park source meaning "no telescope under this roof". It
  stands in for N.I.N.A. only: it can't be set while a N.I.N.A. address is set (the save is refused
  and says so), and if one is ever present anyway it has no effect. Fitted park switches still
  count: a switch that is open or unreadable still means not parked. It satisfies the park check for
  Open, Close (every source, including the automatic close), jog, teach and the calibration run, and
  the park interlock's re-check during a move.
- Q: What does the weather bypass mean? → A: It stands in for a missing weather station only. It
  turns the verdict safe only when both SQMeter sources are off and "no weather source" is the only
  problem. A fitted rain contact that reads wet or unreadable still makes the verdict unsafe (and
  starts the automatic close). It can't be set while an SQMeter source is on (the save is refused);
  if one is on anyway it has no effect. A configured source that is stale, unreachable or offline
  is never bypassed: that is the same doubt a real station gives in the rain.
- Q: What does the daylight bypass mean? → A: While the daylight failsafe is on "alert and close",
  the sun being up no longer refuses opening or closes the roof. The daylight state, its alerts and
  its texts stay; the mode setting itself is kept, so the close comes back when the bypass ends.
- Q: Can the supply check be bypassed? → A: No. Principle I lists supply below its limit as a
  motion fault, and faults are never bypassable. A real motor needs its 12 V supply connected
  anyway, and then the sense reads it; on USB alone (the board reads about 5.5 V) the motor has no
  power to test with. The bench build covers a board on USB.
- Q: Which faults can be bypassed? → A: None. Motor faults, a Hall reading at a rail, the control
  watchdog, over-current, a timeout, the supply and stop-on-fault all stay exactly as they are.
- Q: How does a bypass end? → A: Every bypass shares one deadline. Turning one on (or renewing)
  sets the deadline to now plus `testBox.hours` (1-720 h, default 72 h). After it, every bypass is
  out of effect and the checks are back, without a save; the settings still show them as set and
  expired until the owner turns them off or renews. The deadline needs a set clock: turning a
  bypass on with the clock not set is refused, and after a restart a bypass is out of effect until
  the clock is set again. A restored backup never carries a deadline, so its bypasses are out of
  effect until saved again.
- Q: How is it made visible? → A: A red banner on every page naming each bypass in effect and when
  it ends (or that they expired); a `testBox` object in the roof section of `/api/status`,
  `/ws/status` and the MQTT state document; the dashboard's park and weather rows say "Bypassed
  (test box)" instead of "Parked" or "Safe"; the Alpaca Dome's description starts with "TEST BOX";
  a Home Assistant "Test box bypass" problem sensor with the bypasses as attributes; a log warning at boot
  and on every change; and a new "Test box mode on" alert.
- Q: When does the alert go out? → A: When a bypass comes into effect (turned on, or a new one
  added) and again after every boot while one is in effect (after the alert start-up grace). Its
  level is a setting (default urgent) that can't be off.
- Q: What does the wizard offer? → A: A first screen on its Hardware page: "Is there a telescope under this roof yet?", answered "Yes, or there will
  be" or "No telescope yet (bench setup)". The second sets all three
  bypasses with a red warning and skips the weather source and N.I.N.A. screens; the review says
  the checks are bypassed and until when.
- Q: Can a test box park a real mount? → A: Only if the owner lets it. A box pointed at a live
  N.I.N.A. asks it to park when the weather turns unsafe, as a real install does; a new setting,
  "May ask N.I.N.A. to park" (`park.requestPark`, on by default), turns that off. The
  status and the banner say when it would happen.
- Q: Who can change it? → A: Only an authenticated settings save (`POST /api/config`, which calls
  `requireAuth`). No MQTT, Home Assistant, Alpaca or SQMeter sync path sets it, and a backup restore
  brings no deadline.

## Constitution rules this spec relies on (and the one it changes)

| Principle | Rule | Where it is kept |
|---|---|---|
| I | Every motion fault stops the motor, drops EN and latches; the supply limit is a motion fault | FR-010: nothing here touches a fault, the watchdog or the supply check |
| I | The roof never moves by itself from an unknown position | Unchanged: jog and the calibration run stay attended and authenticated |
| II | Close only onto a confirmed park | **Amended (3.0.0)**: a test box with no telescope may stand in "no telescope" for N.I.N.A., only as an explicit, visible, owner-set, expiring setting (FR-001 to FR-004) |
| II | Every close is checked the same way, re-checked during the close | FR-003: the bypass is part of the one park result every path uses; fitted switches still stop a close |
| III | Doubt resolves to unsafe | **Amended (3.0.0)**: with no weather source at all, the owner may bypass "no weather source"; any reading (rain contact) and any configured source's doubt still count (FR-005, FR-006) |
| III | Alerts, MQTT and the UI never change the verdict | FR-008: they only show the bypass; only a settings save sets it |
| V | Safety rules in `lib/` with native tests | FR-013 |
| VII | Open and Close say why they're unavailable; no new styles | The banner is the existing Note in its bad tone |
| VIII | State changes need `requireAuth` | FR-008 |
| IX | Docs move with behaviour | FR-014 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Try a box on the desk (Priority: P1)

The owner has a new box on the desk: no telescope, no weather station. They turn on test box mode
(the three bypasses) in Settings and can then jog, teach, run the calibration and open and close
the roof, with every fault check still working.

**Why this priority**: it's why the feature exists.

**Independent Test**: on the demo or a board with no N.I.N.A. address and no SQMeter source, turn
the bypasses on; Open, Close, jog and the calibration start are accepted; turn them off and they
are refused again with today's reasons.

**Acceptance Scenarios**:

1. **Given** no N.I.N.A. address, no park switches and the park bypass on, **When** the owner asks
   to close (web, Alpaca, MQTT) or starts a jog, teach or a calibration leg, **Then** the park check
   passes and the park text says it is bypassed for a test box.
2. **Given** both SQMeter sources off, no rain contact and the weather bypass on, **When** the
   owner opens, **Then** the weather check passes and its text says it is bypassed.
3. **Given** daylight on "alert and close", the sun up and the daylight bypass on, **When** the
   owner opens, **Then** it is not refused for daylight and the roof is not closed for daylight.
4. **Given** any bypass on, **When** a motor fault, a Hall rail reading, over-current, a timeout, a
   watchdog trip or a low supply happens, **Then** the roof stops and latches exactly as today.
5. **Given** the bypasses on, **When** the owner turns them off, **Then** every check is back at
   the next evaluation (within about a second).

---

### User Story 2 - It can't be forgotten (Priority: P1)

Every surface shows the bypass while it is in effect.

**Independent Test**: turn a bypass on and look at each surface; turn it off and see them clear.

**Acceptance Scenarios**:

1. **Given** a bypass in effect, **When** any page of the web UI is open, **Then** a red banner
   names each bypass and when it ends.
2. **Given** a bypass in effect, **When** `/api/status`, `/ws/status` or the MQTT state document is
   read, **Then** the roof section's `testBox` says which bypasses are in effect and until when.
3. **Given** the park bypass in effect, **When** the dashboard shows the park, **Then** it says
   "Test box bypass", never "Parked"; likewise the weather row.
4. **Given** a bypass in effect, **When** an Alpaca client reads the Dome's description, **Then** it
   starts with "TEST BOX" and names the bypasses.
5. **Given** Home Assistant discovery on, **When** a bypass is in effect, **Then** the "Test box"
   problem sensor is on, with the bypasses and the deadline as attributes.
6. **Given** alerts set up, **When** a bypass comes into effect, **Then** a "Test box mode on"
   alert is sent; **and** after every restart while one is in effect, it is sent again.

---

### User Story 3 - It ends on its own (Priority: P1)

**Acceptance Scenarios**:

1. **Given** bypasses turned on with 72 h, **When** 72 h pass, **Then** every bypass is out of
   effect, the checks are back, and the banner says they expired.
2. **Given** expired bypasses, **When** the owner saves Renew (or turns a bypass on again), **Then**
   a new deadline starts from now.
3. **Given** the clock is not set, **When** the owner tries to turn a bypass on, **Then** the save
   is refused and says the clock must be set first; **and** after a restart, until the clock is set
   again, the bypasses are out of effect.
4. **Given** a backup with bypasses set, **When** it is restored, **Then** they are shown as set but
   not in effect until saved again.

---

### User Story 4 - It can't hide a real telescope or a real station (Priority: P1)

**Acceptance Scenarios**:

1. **Given** the park bypass on, **When** the owner saves a N.I.N.A. address, **Then** the save is
   refused: "turn off the test box park bypass first".
2. **Given** a N.I.N.A. address, **When** the owner turns the park bypass on, **Then** the save is
   refused likewise; the same both ways for the weather bypass and an SQMeter source.
3. **Given** the park bypass on and a fitted park switch that reads open, **When** a close is asked,
   **Then** it is refused, and a close under way stops as a park-interlock fault.
4. **Given** the weather bypass on and a fitted rain contact that reads wet, **Then** the verdict is
   unsafe and the automatic close starts as today.

---

### User Story 5 - The wizard asks (Priority: P2)

**Acceptance Scenarios**:

1. **Given** the setup wizard, **When** the owner reaches the Hardware page, **Then** the first
   screen asks whether a telescope is or will be under this roof, or this is a test box.
2. **Given** "test box" is chosen, **Then** a red warning explains what is bypassed and for how
   long, the three bypasses are saved, and the weather source and N.I.N.A. screens are skipped;
   the review lists the bypasses.
3. **Given** "a telescope" is chosen, **Then** every bypass is turned off.

### Edge Cases

- A bypass set with the deadline past, or no deadline: out of effect, shown as expired.
- The clock jumps backwards after NTP: the deadline is wall-clock; at most the jump is added.
- A save that only turns one bypass off doesn't renew the deadline; turning one on or sending
  `testBox.hours` does.
- Turning every bypass off clears the deadline.
- Stored settings with a bypass and a N.I.N.A. address or SQMeter source (written by hand or by a
  future version): they load, and that bypass has no effect.
- The automatic close with the weather bypass and a wet rain contact: park (bypassed, so at once)
  and close, as today.
- The bypass expires while the roof is open: the checks are back; with no weather source the
  verdict is unsafe, the automatic close asks for a park, which can't be confirmed without a
  park source, so the roof stays open and the critical "mount not parked" alert goes out (today's
  rule).
- The test box alert is set to off in an old backup: raised to quiet (it can't be off).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Settings gain a `testBox` group: `park`, `weather` and `daylight` (each off by
  default and off after an upgrade), `hours` (1-720, default 72) and a device-owned `until` (the
  deadline, seconds since 1970 UTC, 0 for none) that a save never takes from its body.
- **FR-002**: A bypass is **in effect** only when it is set, the clock is set and the time is before
  `until`. The park bypass also needs no N.I.N.A. address; the weather bypass also needs both
  SQMeter sources off.
- **FR-003**: With the park bypass in effect, the park result ignores N.I.N.A. and counts as
  parked when every fitted park switch reads closed. It is the park result every close path, the
  automatic close, jog, teach, the calibration run and the park interlock use. Its text says
  "bypassed (test box): no telescope under this roof".
- **FR-004**: With the park bypass in effect, a move under way never rides through on a N.I.N.A.
  reading (spec 021): the switches alone are re-checked every step, and the bypass's end (expiry or
  a save) stops a close under way as a park-interlock fault like any lost park.
- **FR-005**: With the weather bypass in effect and "no weather source" the only problem, the
  verdict is safe and its text says "bypassed (test box): no weather source". Any other problem
  (a rain contact wet or unreadable) keeps the verdict unsafe.
- **FR-006**: With the daylight bypass in effect, daylight never wants the roof closed: no open
  refusal, no daylight close; the daylight state, text and alerts stay.
- **FR-007**: A save that turns a bypass on, or sends `testBox.hours`, sets `until` to now plus
  `hours`; it is refused if the clock is not set. A save that leaves no bypass set clears `until`.
  A save that sets the park bypass with a N.I.N.A. address, or the weather bypass with an SQMeter
  source on, is refused with a message saying which to turn off.
- **FR-008**: Only `POST /api/config` (authenticated) changes the bypasses. MQTT, Home Assistant,
  Alpaca and the SQMeter sync never do; a restore never brings a deadline.
- **FR-009**: The roof section of the status and MQTT state documents carries `testBox`: `set` (any
  bypass set), `active` (any in effect), `park`, `weather` and `daylight` (each in effect),
  `until`, `expired` and a `text` naming the bypasses in effect.
- **FR-010**: No fault, the watchdog, the supply check, the drive budget, the timeouts or
  stop-on-fault change. There is no supply bypass.
- **FR-011**: Visibility: the red banner on every page (FR-009's text and deadline, or "expired"
  while set and expired); "Test box bypass" for the park and weather rows on the dashboard;
  the Dome description prefix "TEST BOX (bypassed: ...)"; a Home Assistant `test_box` binary
  sensor (problem) with the `testBox` object as attributes; log warnings at boot and on change.
- **FR-012**: A "Test box mode on" alert event (`test_box`), default urgent, never off, sent when a
  bypass comes into effect and once after each boot while one is in effect, on its own and past the
  master switch, the pause and the send mode (like the critical alerts, at its own level).
- **FR-015**: A setting `park.requestPark` ("May ask N.I.N.A. to park", on by default
  and after an upgrade) decides whether the automatic close sends N.I.N.A. a park request; off, it
  only waits for the park. It is on the Mount park card and on the wizard's test box screen; the
  status (`roof.testBox.parkRequests`) and the banner say when unsafe weather would ask a
  configured N.I.N.A. to park. Settings → Roof → Mount park, the wizard's N.I.N.A. screen and the
  docs warn that a RoofRoof pointed at a live N.I.N.A. can park that mount; the N.I.N.A. screen
  links to the test box screen, so setup never needs a mount parked.
- **FR-013**: The decisions (in effect, the park, verdict and daylight effects, the save stamp, the
  status text, the alert) are pure `lib/` code with native tests; the demo uses the same code.
- **FR-014**: Settings → Roof gets a Test box card (the three bypasses, hours, the deadline, Renew,
  the warning); the wizard gets its test box screen; docs: a Test box user guide page, the
  configuration, REST, MQTT, Alpaca, safety inputs, daylight, alerts and wizard pages; the
  constitution amendment; OpenAPI and contract schemas.

### Key Entities

- **Test box settings**: `testBox.park`, `.weather`, `.daylight`, `.hours`, `.until`.
- **Test box state** (derived, never stored): which bypasses are in effect now, expired or waiting
  for the clock, and the text.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With the three bypasses on and no N.I.N.A., SQMeter, switches or rain contact, Open,
  Close, jog, teach and a calibration start are accepted (native and demo tests).
- **SC-002**: With them off, every one of those is refused with today's reason (native tests).
- **SC-003**: No fault, supply or watchdog behaviour changes with any bypass on (the existing fault
  suites pass unchanged; a native test latches each fault with every bypass on).
- **SC-004**: Every surface in FR-011 shows the bypass within 5 s of the save, the page that saved
  it at once (Vitest, Playwright, native).
- **SC-005**: At the deadline every bypass is out of effect at the next evaluation (native test at
  the boundary second).

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Park bypass = a "no telescope" park source**, exclusive with N.I.N.A.: a save with both is
   refused, and stored settings with both ignore the bypass. No auto-off on a N.I.N.A. reading is
   needed, because the bypass can't coexist with N.I.N.A. at all. Fitted park switches still count.
2. **Weather bypass covers only a missing station**: both SQMeter sources off. A configured
   source's doubt (waiting, stale, unreachable, unreadable, offline) is never bypassed, nor a rain
   contact that is wet or unreadable. This reads "unknown" in the request narrowly: a configured
   source that fails looks the same as a real station failing in the rain.
3. **Daylight bypass** suppresses the daylight close and its open refusal only; the mode setting
   and the daylight alerts stay, so the close comes back on its own when the bypass ends.
4. **No supply bypass.** The supply limit is a Principle I motion fault; a real motor needs 12 V
   connected, and then the sense reads it. On USB the bench build is the tool.
5. **Expiry: one mandatory deadline**, 72 h by default, 1 to 720 h (30 days), renewable at any
   time. It lets a box be tested for days, and nothing can be left bypassed for longer than a
   month without a fresh, deliberate save. Expired bypasses stay set (shown as expired) rather
   than being cleared by the device, so nothing writes the settings behind the owner's back.
6. **The clock must be set** to turn a bypass on; after a restart a bypass is out of effect until
   the clock is set (NTP usually takes seconds). A restored backup brings no deadline.
7. **The test box alert can't be off** (lowest level quiet) and defaults to urgent; it is sent on
   each bypass coming into effect and after every boot while one is in effect, not on expiry (the
   banner, status and log say that). It goes out past the master switch, the pause and the send
   mode, so a test box with alerts off or no imaging app still gets it on any channel set up.
8. **The wizard's test box choice sets the bypasses** (park only with no N.I.N.A. address,
   weather only with no SQMeter source on, saying so when one stays off) and skips the weather
   source and N.I.N.A. screens; choosing "a telescope" turns them all off.
9. **The UI was not mocked up for owner review before building** (constitution workflow); it uses
   only existing components (Note, Toggle, NumberInput, ChoiceGroup), and the owner reviews it on
   the PR.
10. **Constitution 3.0.0 (MAJOR)** on top of 2.1.1. If the owner takes spec 021's change as 3.0.0,
    this becomes 4.0.0 with the same text.
11. **With authentication off**, "authenticated save" means the device's normal settings save, as
    for every other setting; the bypass adds no separate password.
12. **The dashboard pill reads "Test box bypass"**, not "Bypassed (test box)": the design system
    caps pills at 16 characters (DS copy rules); the pill's text line carries the device's
    "bypassed (test box): ..." wording.
13. **Larger JSON pools**: the status document's pool grows from 6144 to 6656 bytes, the MQTT state
    document's from 2048 to 2560 and the settings parse margin by 1 KB, all transient per request.
14. **The expiry is wall-clock time.** A clock stepped back repeatedly (a misbehaving time server)
    could keep a bypass alive past its hours; the banner and the status show the deadline, and a
    restart without a clock suspends every bypass.
15. **Park requests are a setting, not a test-box ban.** `park.requestPark` ("May ask N.I.N.A. to
    park", on by default and after an upgrade) applies to every install; off, the
    automatic close only waits for the park (it closes at once with the park bypass in effect,
    otherwise the roof stays open and the critical alert goes out). Parking a real mount when the
    weather turns unsafe is acceptable to the owner, so the default stays on; the setting is on the
    Mount park card and the wizard's test box screen, and the status and banner say when unsafe
    weather would ask a configured N.I.N.A. to park. Making step 2 of Principle III switchable is
    part of the 3.0.0 amendment.
16. **The wizard never needs a parked mount**: the test box question comes first on the Hardware
    page (before teach and the calibration run, which need the park), and the N.I.N.A. screen links
    back to it and warns about a live N.I.N.A.
