# Feature Specification: Setup Wizard

**Feature Branch**: `spec/015-setup-wizard`

**Created**: 2026-11-11

**Status**: Draft

## Context

A new RoofRoof starts with its own Wi-Fi hotspot and a captive portal that asks for the home
network ([First Boot](../../docs/getting-started/first-setup.md)). Once it has joined, nothing
else guides the owner: the device name and password are on Settings → Device, the time on
Settings → Time, the roof's hardware, the Hall sensor, the motion, the weather sources, N.I.N.A.
and daylight on Settings → Roof, Alpaca on Settings → Network, the alert channels on Settings →
Alerts, and the order to do them in is in the commissioning guide
([Commissioning](../../docs/hardware/commissioning.md)). An owner who misses one (no weather
source, no N.I.N.A. address, no alert channel) has a roof that can't close itself in bad
weather, or can't say it didn't, and nothing on the device points that out.

This spec adds a setup wizard: ten short pages, shown once the device is on the network, that
walk through the essential settings in a sensible order, show the live readings each one needs
(supply volts, park switches, rain contact, the Hall reading against its thresholds) and the
live answer from SQMeter and N.I.N.A., and end with a review that lists every check. Each page
saves through the same settings API and validation as Settings, so a save is refused while the
roof moves, as it is today. The wizard never moves the roof; the motion tests stay in the
commissioning guide, which its last page links.

The pages, in order:

| # | Page | Settings |
|---|---|---|
| 1 | Device | `deviceName`, `auth.*` (a password is strongly recommended) |
| 2 | Site and time | `location.*`, `ntp.timezone`, `ntp.enabled`, `ntp.server1` |
| 3 | Hardware | `roof.minSupplyVolts`, `roof.currentSense*` and its scaling, `park.switches`, `safety.rainContact` |
| 4 | Hall sensor | `roof.hallHighEnd`, `roof.hallHighCounts`, `roof.hallLowCounts`, or "not fitted yet" |
| 5 | Roof motion | `roof.travelMm`, `roof.speedMmPerSecond`, `roof.topDutyPercent` |
| 6 | Weather | `safety.http.*`, `safety.mqtt.*`, with a Test |
| 7 | N.I.N.A. | `park.nina.url` with a Test, `alpaca.enabled` |
| 8 | Alerts | `alerts.enabled` and the channels, with Send test |
| 9 | Daylight | `daylight.mode`, `daylight.sunAltitudeDeg` |
| 10 | Review | every check, and Finish |

The advanced settings (ramps, timeouts, leave-magnet time, slow duty, current limits beyond the
scaling, alert levels and texts, the close-sequence policies of spec 014) stay in Settings.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Which devices see the wizard without asking for it, and what does an upgraded device get?
  → A: A device that starts with no stored settings (new, or after a factory reset) has setup
  state `new`: the first time the dashboard opens after it has joined a network, the wizard
  opens at the page it reached last. Settings saved by a release before this one load as
  `offered`: the dashboard shows a banner offering the wizard, which can be dismissed, and
  nothing opens by itself. Finishing, skipping or dismissing sets `done`; the wizard can be run
  again from the System page at any time.
- Q: Does the wizard open again every time the dashboard opens while setup isn't done? → A: Only
  on the first dashboard visit of a page load. After that (the owner went back to the dashboard)
  the dashboard shows the banner with "Continue setup" and "Dismiss", so the owner is never
  trapped in the wizard.
- Q: Can a page be left without saving? → A: Yes. Back and the page list never save; "Save and
  continue" saves the page (when anything changed) and moves on; "Skip page" moves on without
  saving. The review lists what is still missing either way.
- Q: How does a Test on the weather and N.I.N.A. pages work, and does it send anything? → A: It
  sends nothing new. The device already polls SQMeter every 5 s and N.I.N.A. every 2 s, and a
  saved change to an address makes that poll due at once. Test saves the page if it has unsaved
  changes, then reads a new read-only document, `GET /api/roof/inputs`, with the weather verdict,
  each SQMeter source's state and reason, and the mount as N.I.N.A. last reported it (reachable,
  connected, AtPark, slewing, the reading's age), until a reading newer than the save arrives or
  15 s pass.
- Q: Does the supply step add a calibration setting? → A: No. The divider (100k/22k) is a fact
  of the pin table, not a setting, and a scale factor could hide a low supply. The page shows the
  live volts and the divider, takes the owner's meter reading (not saved) and says whether the
  two agree within 0.2 V, the commissioning guide's tolerance; if not, it points to the divider.
- Q: Which motion values does the wizard start the owner with? → A: The commissioning guide's
  first-run values: while travel, speed and top duty are still the factory defaults, the page
  offers "Use first-run values", which sets the top duty to 40 % (travel and speed stay at their
  defaults until measured). It explains that measuring travel and speed is part of commissioning
  and that the top duty goes back up only after the measured runs.
- Q: What does "not fitted yet" on the Hall page do? → A: It saves nothing on that page (the
  thresholds stay as they are, the defaults on a new device), says that without the sensor the
  roof can't find either end and the automatic close can't work, and links the quick bench test
  without the sensor. The review marks the Hall sensor as a problem until a reading at a magnet
  is seen.
- Q: How strongly is the password pushed? → A: The wizard never refuses to go on without one,
  but page 1 starts with password protection switched on (when it is off and no password is
  set) and says why; turning it off shows a warning that anyone on the network could then change
  settings and move the roof. The review marks a missing password as a problem.
- Q: Where does "run again" live? → A: The System page gets a Setup card with "Run setup wizard"
  and the setup state; there is no System tab in Settings.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | Settings that change motion are refused while the roof moves | FR-012: every wizard save goes through the settings save path, which refuses every save while the roof moves or a close waits |
| I, III | The roof never moves by itself; nothing new moves it | FR-013: the wizard has no Open, Close, Clear fault or other command, and adds no command path |
| VII | Stop is reachable on every page while the roof moves | FR-014: the wizard is a page of the app, under the same Stop bar |
| VII | A greyed-out control always shows its reason | FR-015 |
| VIII | Endpoints that change state call `requireAuth`; secrets stay masked | FR-016, FR-017: the new endpoint only reads, changes nothing, sends nothing and returns no secrets; saves keep `requireAuth` and the masked secrets |
| VI | Request handlers never block on the network | FR-017: the endpoint copies the last readings; no outgoing request |
| Platform | A migration never turns on a motion feature | FR-002: the new settings move nothing; the first-run values lower the top duty, never raise anything |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A new device walks the owner through its setup (Priority: P1)

The owner has just joined a new RoofRoof to the home network from the hotspot. Opening its
address shows the setup wizard. Page by page the owner names the device and sets a password,
enters the site and time zone, says which hardware is fitted while watching the live readings,
calibrates the Hall thresholds against the live reading, enters the roof's travel with the
first-run duty, points the weather and N.I.N.A. pages at SQMeter and the imaging PC and sees
each one answer, turns on an alert channel and sends a test, picks the daylight mode, and
finishes on a review that lists every check and links the commissioning guide.

**Why this priority**: This is the feature: a new device set up in one sitting, in a safe order.

**Independent Test**: In the demo, restart the wizard from the Demo panel, go through all ten
pages saving each, and check that the settings are what was entered (Settings shows them), the
review lists every check, and Finish returns to the dashboard with no banner.

**Acceptance Scenarios**:

1. **Given** a device with setup state `new` that has joined a network, **When** the dashboard
   opens, **Then** the wizard opens at the page saved last (page 1 the first time).
2. **Given** any page with changes, **When** the owner presses "Save and continue", **Then** the
   page's settings are saved with the same validation as Settings and the next page opens; a
   refused save keeps the page open with the device's reason.
3. **Given** page 3, **When** the readings arrive, **Then** the supply volts, the park switches'
   states and the rain contact's state are shown live, as the device reads them.
4. **Given** page 4, **When** the Hall reading changes, **Then** the scale shows it against the
   thresholds as typed, before saving.
5. **Given** the review page, **When** it opens, **Then** it lists every check with its state and
   what to do, and Finish sets setup state `done` and returns to the dashboard.

---

### User Story 2 - Test SQMeter and N.I.N.A. from the wizard (Priority: P1)

On the weather page the owner types SQMeter's address and presses Test: the page shows within a
few seconds whether SQMeter answers and what it says (safe, or unsafe and why). On the N.I.N.A.
page the same for the imaging PC: whether N.I.N.A. answers, whether the mount is connected, at
park and slewing.

**Why this priority**: A wrong address is the commonest set-up mistake, and the roof can't close
by itself without both.

**Independent Test**: In the demo, set a weather address and press Test: "Safe" with the
SQMeter source good. Turn on "Unsafe" in the Demo panel and Test again: "Unsafe" with the
reason. On the N.I.N.A. page Test shows the mount connected and at park; "Park the mount" off
shows "Not at park".

**Acceptance Scenarios**:

1. **Given** unsaved changes on the page, **When** Test is pressed, **Then** the page is saved
   first and the result shown is from a reading taken after the save.
2. **Given** SQMeter not answering, **When** Test is pressed, **Then** the result says the
   source is unreachable with the device's reason, within 15 s.
3. **Given** N.I.N.A. answering with the mount disconnected, **When** Test is pressed, **Then**
   the result shows N.I.N.A. reachable and the mount not connected.
4. **Given** the roof moving, **When** Test is pressed with unsaved changes, **Then** the save is
   refused with the device's reason and no reading is claimed as new.

---

### User Story 3 - Skip, resume or run it again (Priority: P2)

An owner without the hardware to hand skips the Hall page, closes the browser, and comes back
the next day to carry on where they left off. Another skips the whole wizard with a clear
warning and runs it later from the System page.

**Why this priority**: Setup rarely happens in one sitting, and an owner must never be trapped.

**Independent Test**: Save page 3, reload: the wizard opens at page 4. Skip setup: the warning
is shown, confirming returns to the dashboard with no banner, and System → Setup → "Run setup
wizard" opens page 1.

**Acceptance Scenarios**:

1. **Given** setup state `new` and pages 1-3 saved, **When** the page is reloaded, **Then** the
   wizard opens at page 4.
2. **Given** the wizard open, **When** the owner returns to the dashboard, **Then** the dashboard
   shows "Setup isn't finished" with "Continue setup" and "Dismiss", and doesn't open the wizard
   again until the next page load.
3. **Given** "Skip setup", **When** pressed, **Then** a warning says what an unfinished setup
   leaves undone and asks to confirm; confirming sets `done`.
4. **Given** setup state `done`, **When** "Run setup wizard" is pressed on the System page,
   **Then** the wizard opens at page 1 with the device's current settings.

---

### User Story 4 - An upgraded device is offered the wizard, not forced into it (Priority: P2)

An owner who set the roof up with an earlier release installs this one. The dashboard shows a
banner offering the setup wizard; the owner dismisses it, or runs it to check the settings.

**Why this priority**: A set-up device must not be interrupted, but its owner may like the review.

**Independent Test**: Load settings saved by the previous release: the setup state is `offered`,
the dashboard shows the banner and nothing opens; Dismiss sets `done` and the banner goes.

**Acceptance Scenarios**:

1. **Given** stored settings with no setup section, **When** the device starts, **Then** the
   setup state is `offered` and every other setting is as stored.
2. **Given** `offered`, **When** the dashboard opens, **Then** a banner offers the wizard with
   "Run setup wizard" and "Dismiss", and the wizard doesn't open by itself.

---

### User Story 5 - The wizard never moves the roof (Priority: P1)

Whatever page is open, the wizard offers no way to open, close or clear a fault; the Stop bar is
there while the roof moves, and a save is refused while it moves.

**Why this priority**: Constitution I and III; the motion tests belong to the attended
commissioning, not to a form.

**Independent Test**: In the demo, start an open from the dashboard and go to the wizard: the
Stop bar shows, the page says saves wait for the roof, "Save and continue" is refused with the
device's reason. No page has Open, Close or Clear fault.

**Acceptance Scenarios**:

1. **Given** the roof moving, **When** any page is saved, **Then** the device refuses the save
   and the page shows why, and the Stop bar is on the page.
2. **Given** any page, **When** it is inspected, **Then** it has no Open, Close, Clear fault or
   motion test control, and the last page links the commissioning guide and its unattended gate.

---

### Edge Cases

- The device is still in hotspot mode: the wizard doesn't open by itself (the Wi-Fi page is the
  hotspot's job); it can still be reached from the System page.
- Password protection is turned on in page 1: the browser asks for the new password on the next
  save, as Settings does today.
- The page is reloaded mid-page with unsaved changes: they are lost, as in Settings; the saved
  step is where it opens.
- The status or the inputs document can't be read: the readings show "No reading" and the
  review marks those checks unknown, never as passing.
- The roof stops part-way while a page is open: the page says a save then makes the position
  unknown, as Settings → Roof does.
- No weather source is set: the review says the verdict will stay unsafe (no source) and the
  roof will keep refusing to open.
- No N.I.N.A. address: the review says the roof can't close by itself (the park can't be
  confirmed).
- No alert channel: page 8 and the review warn strongly that critical alerts will reach no one.
- A Test while the device's poll fails to answer within 15 s: "No new reading yet" and the last
  known reading with its age, never a stale reading shown as new.
- The setup state is changed by another browser meanwhile: the wizard works on the settings it
  loaded, as Settings does; Finish sets `done` whatever the other browser did.
- A backup restore carries the backup's setup state.

## Requirements *(mandatory)*

### Functional Requirements

**Settings**

- **FR-001**: The device MUST keep `setup.state` (`new`, `offered` or `done`) and `setup.step`
  (0 to 9, the page to resume at). A device with no stored settings MUST start `new`, step 0.
  A value outside these MUST be refused by the device and the web UI.
- **FR-002**: Stored settings with no `setup` section (saved by an earlier release) MUST load as
  `offered`, step 0, with every other setting unchanged. The setup settings change no motion
  setting and need no restart.

**When the wizard shows**

- **FR-003**: With setup state `new`, the first dashboard visit of a page load MUST open the
  wizard at `setup.step`, unless the device is in hotspot mode.
- **FR-004**: With setup state `new` or `offered`, the dashboard MUST show a banner (outside the
  card grid, like the bench banner): for `new` "Setup isn't finished" with "Continue setup" and
  "Dismiss"; for `offered` an offer with "Run setup wizard" and "Dismiss". Dismiss sets `done`.
  With `done` nothing is shown.
- **FR-005**: "Skip setup" on any page MUST show a warning saying the roof may not be able to
  close itself in bad weather or alert anyone until the essentials are set, and set `done` only
  once confirmed.
- **FR-006**: The System page MUST have a Setup card showing the setup state and "Run setup
  wizard", which opens page 1 whatever the state.

**Pages**

- **FR-007**: Each page MUST save only through the settings API (`POST /api/config`) with the
  same validation as Settings (the web UI's schema first, then the device's), and include
  `setup.step` set to the next page. "Save and continue" saves when the page has changes and
  then moves on; Back, the page list and "Skip page" never save.
- **FR-008**: The pages MUST hold the settings in the table in Context, reusing the Settings
  controls and their hints and ranges, and:
  - page 1 starts with password protection on when it is off and no password is set, and warns
    when it is turned off;
  - page 3 shows the live supply volts with the 100k/22k divider and the minimum, takes a meter
    reading that isn't saved and says whether it agrees within 0.2 V, shows each fitted park
    switch's live state and the rain contact's live state, and the current-sense scaling when
    current sense is on;
  - page 4 shows the live Hall reading against the thresholds as typed (spec 009's scale), and
    offers "Not fitted yet", which saves nothing on the page, says the automatic close can't
    work until the sensor is fitted and links the quick bench test;
  - page 5 offers "Use first-run values" (top duty 40 %) while travel, speed and top duty are the
    factory defaults, and says measuring travel and speed is part of commissioning;
  - page 7 explains that N.I.N.A.'s sequence opens and closes the roof through the Alpaca Dome
    and the controller is the failsafe behind it, and warns when Alpaca is off;
  - page 8 warns while alerts are off or no channel is on that critical alerts reach no one, and
    offers Send test per channel (spec 007's test, after a save);
  - page 9 explains the three modes.
- **FR-009**: The weather and N.I.N.A. pages MUST have Test: it saves the page if it has
  changes (refused saves end the test with the reason), then reads `GET /api/roof/inputs` once a
  second until a reading taken after the save arrives, for at most 15 s, and shows:
  - weather: the verdict (safe, unsafe or unknown), and for each SQMeter source switched on its
    state (good, waiting, stale, unreachable, unreadable, offline) and the device's reason;
  - N.I.N.A.: whether it answered (or why not), and the mount's connected, at park and slewing
    states with the reading's age.
  A reading not newer than the save is never shown as the test's result.
- **FR-010**: The review page MUST list one check per essential (password, site, clock, supply,
  park switches, rain contact, Hall sensor, motion values, weather, N.I.N.A., Alpaca, alerts,
  daylight), each marked OK, attention or problem (or unknown without a reading), with the
  reason and a link to its page; and Finish, which sets `done` (and `setup.step` 0) and shows a
  restart note when a saved change needs one (Alpaca), as Settings does.
- **FR-011**: The last page MUST link the commissioning guide and its gate for unattended use,
  and say that the motion tests are done there, attended.

**Safety**

- **FR-012**: Every wizard save MUST go through the device's settings save (`requireAuth`, the
  device's validation, and spec 010's flash-write hold that refuses a save while the roof moves
  or a close waits); while the roof moves or is stopped part-way the page MUST say so, as
  Settings → Roof does.
- **FR-013**: The wizard MUST NOT offer Open, Close, Stop-as-a-test, Clear fault or any other
  roof command, and MUST NOT add a command path to the device.
- **FR-014**: The wizard MUST be a page of the app, so the Stop bar shows on it while the roof
  moves.
- **FR-015**: A disabled control on a page (Test while a save is under way, Send test before a
  save) MUST show why.

**The inputs document**

- **FR-016**: The device MUST answer `GET /api/roof/inputs` (GET only, no body) with the weather
  verdict and its text, each SQMeter source (HTTP and MQTT: on, state, age, the stale limit,
  SQMeter's safe flag and reason flags from its last good reply, the reason text), the rain
  contact, the park result and its text, and N.I.N.A.'s last reading (state, connected, at park,
  slewing, its age, the failure text). It MUST contain no address, credential or secret.
- **FR-017**: The endpoint MUST only copy the readings the device already keeps (refreshed once
  a second on the main loop), MUST NOT send any request to SQMeter, N.I.N.A. or anywhere else,
  MUST NOT block on the network or the roof, and MUST NOT change any state. It is open like
  `/api/status` (readings only) and listed in the route registry, the OpenAPI document and the
  contract schemas.
- **FR-018**: The demo MUST answer the same document from its simulated SQMeter and N.I.N.A.,
  written by the same code as the firmware.

**UI and docs**

- **FR-019**: The wizard MUST follow the design system (constitution VII): the existing card,
  button and settings controls, no new button or card styles, every string through i18n with a
  context note, WCAG 2.2 AA (a page heading, the page list as navigation with the current page
  marked, focus moved to the page heading on each page change, readings announced politely) and
  phone width.
- **FR-020**: The demo MUST start with setup `done`, and its Demo panel MUST have "Restart setup
  wizard", which sets `new`, step 0, and opens the wizard.
- **FR-021**: The docs MUST describe the wizard (first boot, the pages, skip, resume, run again,
  the banner on upgrade), the setup settings, the inputs endpoint, and link it from first boot
  and commissioning; the screenshots MUST include the wizard.

### Key Entities

- **Setup state**: `new`, `offered` or `done`, and the page to resume at.
- **Wizard page**: a set of settings, its live readings and its checks.
- **Inputs document**: the weather verdict, the SQMeter sources, the rain contact, the park result
  and N.I.N.A.'s reading, as the device last saw them.
- **Review check**: an essential, its state (OK, attention, problem, unknown), its reason and its
  page.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (each page's content is in `SettingsCard`s, the review in a card), DS-02
  (each check's state is a pill), DS-03 (live readings are `ReadingRow`s), DS-04 (explanations
  behind "?"), DS-06 (a Test's progress and outcome in the card that started it), DS-07 (saves
  confirmed by the page moving on; a lasting problem is a Note), DS-09 (phone width), DS-20 to
  DS-28 (copy). The banner is a `Note` in the slot the bench banner uses, outside the card grid
  (DS-01). No new button or card styles (constitution VII); the page list reuses the settings
  tab styles.
- **Mockup**: not reviewed. The owner was not available; the pages are built from existing
  cards, rows and controls only. The owner reviews the wizard in the demo before merging (listed
  under the PR's pending checks).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An owner can go from a freshly joined device to the review page in under 15 minutes
  in the demo, saving every page, without opening Settings.
- **SC-002**: Every essential setting in the Context table is reachable from the wizard, and
  every check on the review page has a test for each of its states.
- **SC-003**: No wizard page renders an Open, Close or Clear fault control (tested on every page),
  and a save while the roof moves is refused (tested against the device's save path and in the
  demo).
- **SC-004**: A settings document from each earlier release loads as `offered` with every other
  setting unchanged, and a device with no stored settings starts `new`.
- **SC-005**: The inputs endpoint triggers no outgoing request (its handler only copies), returns
  no secret, and matches its contract schema in the demo.
- **SC-006**: The full local check (firmware build, native tests, web tests, Playwright with the
  accessibility scan on every page at phone width, quality gate, docs build, flash and UI
  budgets) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Three setup states.** `new` (no stored settings) opens the wizard once per page load;
   `offered` (settings from an earlier release) only shows a banner; `done` shows nothing. An
   upgraded, working roof is never interrupted, and a new one is never left without guidance.
2. **Skip and Dismiss mean done.** After a confirmed warning, nothing reminds the owner again; the
   System page's Setup card runs it at any time. Repeated nagging would be dismissed unread.
3. **The wizard opens once per page load**, then becomes a banner, so the owner can always reach
   the dashboard and Stop.
4. **No new outgoing request for Test.** The device already polls both; Test saves and then reads
   what the poll brings back. A probe endpoint would add an outgoing request on demand, which
   needs rate limiting and its own time limits; reading the poll needs neither. So the endpoint
   isn't rate-limited: it copies a few readings, like `/api/status`, and is bounded by the web
   server's connection limit.
5. **The inputs endpoint is open** like `/api/status`: it returns readings and texts, no address
   and no secret, and changes nothing.
6. **No supply calibration factor.** The divider is a pin-table fact; the page compares the
   reading with the owner's meter (0.2 V, the commissioning guide's tolerance) and points to the
   divider when they disagree. A factor could make a low supply read as fine.
7. **First-run values lower only the top duty (40 %).** Travel and speed stay at the defaults
   until measured; the leave-magnet time and the other first-move settings stay in the
   commissioning guide, which sets them for the attended first moves.
8. **The password is strongly recommended, not required.** Page 1 starts with it on, warns when
   it is turned off, and the review marks it as a problem; requiring it would stop an owner on a
   bench with no network risk, and the constitution designs for a trusted LAN.
9. **"Not fitted yet" leaves the Hall thresholds alone** and the review keeps the Hall check as a
   problem until a magnet is read, since without it the roof can't find either end.
10. **Run again lives on the System page**, the nearest thing to "Settings → System" the UI has.
11. **The demo starts with setup done** (the demo is a working observatory) and offers "Restart
    setup wizard" in its panel.
12. **The UI mockup review is pending**: the owner reviews the wizard in the demo build before
    merging.

## Out of scope

- The Wi-Fi join itself (the hotspot's captive portal).
- Any motion test, Open or Close in the wizard; ConformU; the attended dry runs.
- Advanced settings (ramps, timeouts, slow duty, alert levels and texts, MQTT and Home Assistant,
  the close-sequence policies of spec 014).
- An on-demand probe of a typed address that hasn't been saved.
