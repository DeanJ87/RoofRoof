# Feature Specification: Dashboard and Settings

**Feature Branch**: `spec/009-dashboard-settings`

**Created**: 2026-11-11

**Status**: Draft

## Context

Specs 002 to 008 built the roof and its interfaces. The web UI can already send roof commands
(`/api/roof/open`, `close`, `stop`, `clear-fault`, each with the `X-RoofRoof: 1` header), and the
status document's `roof` section carries everything the roof knows: state, position, the Hall
reading, supply, current, park switches, rain contact, the fault, the automatic close's phase and
the weather and park checks as the roof's command checks see them (spec 008). The settings model
holds the roof's motion settings, the weather sources and the park settings, but none of it is on
a page: the dashboard has only the Status and Device cards, and Settings has no Roof tab.

This spec puts the roof in the web UI, in SQMeter's design system:

- **A Roof card on the dashboard**: the state, a position bar, Open, Close and Stop, the fault and
  Clear fault, the weather verdict and the park check, and the live Hall reading with its
  thresholds drawn for calibration. Stop is never disabled; Open and Close say why when they are
  unavailable.
- **Stop on every page while the roof moves.**
- **Roof problems on the Status card**: a latched fault and an automatic close that can't finish.
- **A Roof tab in Settings**: travel, speed, timeouts, ramps, duty, direction, Hall, current sense
  and supply, then the weather sources and the park (N.I.N.A. and the park switches), including the
  park time limit.
- Accessibility, the dashboard inventory, screenshots, Playwright and the docs.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Are Open and Close usable while the live data is stale or the connection is down? → A: No.
  Without fresh data the page can't know the roof's state, so a button that moves the roof would
  act on old news. Both are disabled with the reason ("No live data"). Stop stays enabled: it never
  depends on the state.
- Q: Should the page predict refusals, or just send and show the device's answer? → A: Both. The
  page disables Open and Close for every reason it can read from the status document (fault,
  weather not safe, mount not parked, supply low or unreadable, Hall not settled, already at that
  end, the setup hotspot for Open, a close already waiting), each with its reason. The device stays
  the authority: whatever it refuses is shown under the buttons with its own text.
- Q: How are the platform refusals `busy`, `restarting` and `hotspot` shown? → A: `hotspot` is
  predicted from the status document (the hotspot flag) and disables Open with its reason.
  `restarting` disables Open and Close with its reason until fresh live data arrives after the
  restart (or 30 s pass). `busy` is a moment's wait (an upload, a settings save, an MQTT connect):
  the reason is shown under the buttons and they stay usable so the person can try again.
- Q: Where does Stop live while the roof moves on other pages? → A: A bar pinned to the bottom of
  every page while the roof is opening or closing, or a close is waiting to start, with one Stop
  button. It stays while the live data is stale if the roof was last seen moving. It is not in the
  header and not between the header and the cards (DS-01).
- Q: Does Stop ask for confirmation? → A: No. Stop is sent at once, every press, and is never
  disabled or replaced by a busy label. Open and Close don't ask either: each is one deliberate
  press, and the device checks it.
- Q: Stop while the automatic close is waiting for the park or closing stands the close down
  (spec 005). Should the page warn? → A: Yes: while the automatic close is under way the card says,
  in a note, that Stop calls it off. Stop itself stays enabled.
- Q: What does the Status card show for the roof? → A: Only problems (DS-07, DS-08): a row for a
  latched fault and a row for an automatic close that is held by a person, can't move or has
  halted. The Roof card shows the state; the Status card doesn't repeat it.
- Q: Can roof settings be edited while the roof moves? → A: They can be edited, but the tab says
  that a save is refused while the roof moves, and the device refuses it. While the roof is stopped
  part-way, the tab says that saving makes the position unknown.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | The motor moves only through the device's own checks; the UI never decides a move | FR-004, FR-006: the page only predicts; the device decides |
| I | Moving from an unknown position needs a person's explicit, authenticated command | FR-008: the note when the position is unknown; the command carries the person's login |
| I | Only a person clears a fault | FR-010: Clear fault on the Roof card |
| I | Settings that change motion are refused while the roof moves | FR-021 |
| II | Close only onto a parked mount: the UI never offers Close as available while the park check fails | FR-005 |
| III | The UI reports the verdict and can never change it | FR-011: read-only weather and park |
| III | Every refusal states the measured value and the limit, or the input that failed | FR-006, FR-007 |
| VII | Stop reachable on every page while the roof moves, and never disabled | FR-003, FR-012 |
| VII | Open and Close show why they are unavailable | FR-004, FR-005 |
| VII | SQMeter's design system, phone width, WCAG 2.2 AA, i18n | FR-024 to FR-027 |
| VIII | Every command that moves the roof or clears a fault needs the login and the command header | FR-009 |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Open, close and stop the roof from the dashboard (Priority: P1)

The owner opens the dashboard, on a laptop or a phone in the dark, sees whether the roof is open
or closed and where it is, and opens, closes or stops it. When Open or Close can't be used, the
button is disabled and the reason is next to it. Stop always works.

**Why this priority**: it is the reason the UI exists; everything else supports it.

**Independent Test**: with the demo and the component tests: the Roof card shows the state and
position; Open and Close are enabled or disabled with a reason for each roof and safety state;
Stop is enabled in every state; each press sends the matching command with the command header.

**Acceptance Scenarios**:

1. **Given** the roof is closed, the weather is safe and the live data is fresh, **When** the owner
   presses Open, **Then** the page sends the open command and the card shows the roof opening, with
   the position bar moving.
2. **Given** the roof is opening, **When** the owner presses Stop, **Then** the stop command is sent
   at once, and Stop could be pressed again straight away.
3. **Given** the weather verdict is unsafe, **When** the owner looks at the card, **Then** Open is
   disabled and the reason names the verdict's reason.
4. **Given** the mount isn't confirmed parked, **When** the owner looks at the card, **Then** Close
   is disabled and the reason says what failed (for example a park switch open).
5. **Given** the live data has gone quiet or the connection is down, **When** the owner looks at
   the card, **Then** Open and Close are disabled with "No live data" and Stop is enabled.
6. **Given** the owner presses Open and the device refuses it, **When** the answer arrives, **Then**
   the card shows the device's reason under the buttons.
7. **Given** the device answers `restarting`, **When** the owner looks at the card, **Then** Open
   and Close are disabled with that reason until live data arrives again.
8. **Given** the setup hotspot is on, **When** the owner looks at the card, **Then** Open is
   disabled with the hotspot reason, and Close and Stop are not affected by it.
9. **Given** the automatic close is waiting for the park or closing, **When** the owner looks at the
   card, **Then** a note says that Stop calls the automatic close off.

---

### User Story 2 - Stop from any page (Priority: P1)

While the roof moves, the owner is on Settings or Updates. A Stop button is pinned to the bottom of
the page.

**Why this priority**: constitution VII; the person must never have to find the dashboard to stop
the roof.

**Independent Test**: a component test of the layout with a moving roof in the live data: the bar
and Stop are present on each page; with the roof at rest they are gone.

**Acceptance Scenarios**:

1. **Given** the roof is opening or closing, or a close is waiting to start, **When** any page is
   open, **Then** a Stop button is visible at the bottom of the page and sends the stop command.
2. **Given** the live data goes quiet while the roof was moving, **When** the page is open,
   **Then** the Stop bar stays.
3. **Given** the roof is at rest, **When** any page is open, **Then** there is no Stop bar.
4. **Given** the settings page has unsaved changes while the roof moves, **When** both bars show,
   **Then** neither covers the other.

---

### User Story 3 - See the fault, the checks and the Hall reading, and clear a fault (Priority: P1)

After a fault the owner sees what happened, with the measured value and the limit, and clears it
once the cause is fixed. The card shows the weather verdict and the park check as the roof sees
them, the supply, the motor current when current sense is on, the park switches and the rain
contact. During calibration the owner watches the live Hall reading against the thresholds.

**Why this priority**: a fault locks out motion until a person clears it (constitution I); without
this the owner needs curl.

**Independent Test**: component tests with each fault and check state; the demo's healthy roof for
the readings.

**Acceptance Scenarios**:

1. **Given** a latched fault, **When** the owner opens the dashboard, **Then** the Roof card shows
   the fault's text (with the measured value and the limit), a Clear fault button, and Open and
   Close disabled because of the fault.
2. **Given** a latched fault, **When** the owner presses Clear fault, **Then** the clear-fault
   command is sent with the command header, and the device's refusal, if any, is shown.
3. **Given** a roof at rest, **When** the owner looks at the card, **Then** the Hall reading is drawn
   on a scale with the low and high thresholds marked and the reading's level named.
4. **Given** the weather is unsafe because of rain, **When** the owner looks at the card, **Then**
   the weather row shows "Unsafe" and its reason behind "?".

---

### User Story 4 - Set up the roof in Settings (Priority: P2)

The owner sets the measured travel and speed, the ramps, the Hall polarity and thresholds, current
sense, the minimum supply, the weather sources and the park sources and time limit, in a Roof tab.

**Why this priority**: the roof can't be commissioned without it, but the settings exist through
the API already.

**Independent Test**: the settings component tests: every roof, weather and park setting is on the
Roof tab, validated with the device's ranges, saved with the rest of the config, and errors take the
owner to the Roof tab.

**Acceptance Scenarios**:

1. **Given** the Roof tab, **When** the owner changes the travel and saves, **Then** the save carries
   the new value and the device's answer is shown.
2. **Given** a value out of range (for example a slow duty above the top duty), **When** the owner
   saves, **Then** the save is refused in the page with the message next to the field, and the Roof
   tab shows its error dot.
3. **Given** the Hall settings, **When** the owner edits a threshold, **Then** the live Hall reading
   is drawn against the edited thresholds before saving.
4. **Given** the roof is moving, **When** the owner opens the Roof tab, **Then** a note says saves are
   refused while the roof moves.
5. **Given** the SQMeter MQTT source is on while MQTT is off, **When** the owner looks at it,
   **Then** it says it isn't in effect and offers to go to the MQTT settings (settings dependency
   D-40).
6. **Given** the park time limit, **When** the owner sets 20 s, **Then** the page refuses it with the
   device's range (30 to 1800 s).

---

### User Story 5 - Roof problems on the Status card (Priority: P2)

The Status card lists what needs a look. A latched fault and an automatic close that can't finish
are among them.

**Why this priority**: the Status card is where the owner looks first; a stuck close in bad
weather is the most important thing to notice.

**Independent Test**: unit tests of the glance items for each fault and close phase.

**Acceptance Scenarios**:

1. **Given** a latched fault, **When** the dashboard is open, **Then** the Status card has a "Roof"
   row with "Fault" and the fault's text behind "?".
2. **Given** the automatic close is held by a person, can't move or has halted, **When** the
   dashboard is open, **Then** the Status card has an "Automatic close" row with that state.
3. **Given** a healthy, closed roof, **When** the dashboard is open, **Then** the Status card shows
   no roof row and still says "All good".

### Edge Cases

- No `roof` section in the status (an older firmware, or the controller didn't start): the Roof card
  says the roof isn't reporting, Open and Close are disabled with that reason, and Stop stays.
- Position unknown: the bar shows no fill and "Unknown"; Open and Close stay usable for the person
  (a move from unknown is slow, to the first magnet) and a note says so.
- A command request fails to reach the device: the card says the command wasn't confirmed; for Stop
  it says so in the bad tone.
- Stopped part-way: Open and Close both usable (from a known estimate).
- A close waiting to start (`closePending`): Close is disabled with "Close waiting to start"; Stop
  and Open stay (either cancels it).
- Several presses of Stop: each is sent.
- An answer `202` with `pending` (a waiting close): shown as a note with the device's message.
- `401` (login needed) or `403` (missing header): shown as the device's error.
- Thresholds missing from the config (config not loaded yet): the Hall scale is drawn without the
  threshold marks.
- Phone width: buttons wrap without hiding Stop; the Stop bar sits above the demo panel and the save
  bar.

## Requirements *(mandatory)*

### Functional Requirements

**Roof card (dashboard)**

- **FR-001**: The dashboard MUST have a Roof card showing the roof's state as a pill, the position
  as a bar with the percentage open (or "Unknown"), and its id in the default card order after the
  Status card.
- **FR-002**: The Roof card MUST have Open, Close and Stop buttons, in that order of reading, with
  Stop styled as the destructive action.
- **FR-003**: Stop MUST never be disabled, never show a busy state, and send the stop command on
  every press, whatever the live data, the state or a request in flight.
- **FR-004**: Open MUST be disabled, with its reason shown, when: there is no live data (no status
  yet, the connection is down, or no message for more than 5 s); the roof isn't reporting; a
  `restarting` refusal is in force; a fault is latched; the roof is open or opening; the weather
  verdict isn't safe (the reason is the verdict's text); the setup hotspot is on; the supply is low
  or unreadable; or the Hall reading hasn't settled.
- **FR-005**: Close MUST be disabled, with its reason shown, when: there is no live data; the roof
  isn't reporting; a `restarting` refusal is in force; a fault is latched; the roof is closed or
  closing; a close is waiting to start; the park check fails (the reason is its text); the supply is
  low or unreadable; or the Hall reading hasn't settled.
- **FR-006**: A refused command (`409`) MUST show the device's reason text under the buttons. For
  `busy` the buttons stay usable; for `restarting`, Open and Close are disabled until a live message
  arrives after the connection was lost, or 30 s pass; for `hotspot`, Open is disabled while the
  hotspot flag says so.
- **FR-007**: A failed request (no answer), `401`, `403` and a `202` pending answer MUST each be
  shown under the buttons as a one-line note.
- **FR-008**: While the position is unknown, the card MUST say that a move goes slowly to the first
  magnet. While the automatic close is waiting for the park, closing or waiting to retry, it MUST
  say that Stop calls the automatic close off.
- **FR-009**: Every roof command from the UI MUST go through the one helper that adds the
  `X-RoofRoof: 1` header, as a `POST` with no body; the browser's login applies.
- **FR-010**: With a latched fault, the card MUST show the fault's text and a Clear fault button that
  sends the clear-fault command.
- **FR-011**: The card MUST show, read-only: the weather verdict (pill, reason behind "?"), the park
  check (pill, reason behind "?"), the automatic close phase when it isn't idle, the supply voltage
  and its minimum, the motor current and its limit while current sense is on, the park switches
  when fitted, and the rain contact when fitted.
- **FR-012**: While a command is in flight, Open and Close MUST be disabled with a busy label; Stop
  MUST NOT.
- **FR-013**: The card MUST draw the live Hall reading on its 0 to 4095 scale, with the saved low
  and high thresholds marked and the reading's level named, as a labelled image whose text
  alternative states the reading and the thresholds.

**Stop on every page**

- **FR-014**: Every page MUST show a bar pinned to the bottom with a Stop button while the last live
  data says the roof is opening or closing, or a close is waiting to start, including while that
  data has gone stale. The bar MUST NOT be in the header or between the header and the dashboard
  cards, and MUST NOT cover the settings save bar or be covered by the demo panel button.

**Status card**

- **FR-015**: The Status card MUST show a "Roof" row with a problem pill while a fault is latched,
  the fault's text behind "?", linking to the dashboard's Roof card.
- **FR-016**: The Status card MUST show an "Automatic close" row while the close phase is
  `heldByPerson` (note), `cannotMove` or `halted` (problem), with its explanation behind "?".
- **FR-017**: Every field of the status document's `roof` section MUST be mapped in the dashboard
  inventory: shown (with a test) or not shown (with a reason).

**Roof settings tab**

- **FR-018**: Settings MUST have a Roof tab with: Motion (travel, speed, timeout margin, time to leave
  a magnet, ramp up, ramp down, top duty, slow duty, open direction), Hall sensor (which end reads
  high, high threshold, low threshold), Current sense (on, maximum current, amps per volt, minimum
  running current), Supply (minimum volts), Weather sources (SQMeter HTTP on and URL, SQMeter MQTT
  on and topic, stale time, rain contact) and Park (N.I.N.A. URL, park switches fitted, park time
  limit).
- **FR-019**: Each field MUST be validated with the device's ranges and cross rules before saving,
  with the message next to the field, and validation errors under `roof`, `safety` and `park` MUST
  count against the Roof tab.
- **FR-020**: The Hall sensor card MUST draw the live reading against the thresholds as edited.
- **FR-021**: While the roof is opening or closing, the Roof tab MUST say that saves are refused;
  while it is stopped part-way, that saving makes the position unknown.
- **FR-022**: The SQMeter MQTT source MUST show its settings dependencies (D-40 MQTT off, D-41 MQTT
  not connected) with the fix.
- **FR-023**: A save MUST carry the roof, weather and park sections when the device sent them, and
  MUST NOT add them when it didn't.

**Design, accessibility and docs**

- **FR-024**: The UI MUST use the shared components (`Card`, `Pill`, `ReadingRow`, `MetricTile`,
  `Button`, `Note`, `InfoTip`, settings controls) and tokens; any new style is a layout rule or a
  token, not a new button or card style.
- **FR-025**: Every new string MUST be in `en.json` with a context note, and meet DS-20 to DS-28.
- **FR-026**: The Roof card, the Stop bar and the Roof tab MUST pass the axe checks and work at
  320 px wide.
- **FR-027**: The user guide (dashboard, configuration), the calibration page and the screenshots
  list MUST describe the new card, bar and tab.

### Key Entities

- **Roof view**: what the page derives from the status: each button's availability and reason, the
  notes, the pills and the Hall scale.
- **Command result**: the answer to one command, kept until the state changes or 15 s pass (30 s for
  `restarting`).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In every combination the tests cover (each roof state, fault, weather verdict, park
  check, supply state, live-data state, hotspot, pending close and refusal), Stop is enabled, and
  each disabled Open or Close has a reason shown.
- **SC-002**: No roof command leaves the page without the command header (tested for all four).
- **SC-003**: With the roof moving, a Stop button is visible on all six pages.
- **SC-004**: Every roof, weather and park setting can be set from the Roof tab; the page refuses
  every out-of-range value the device refuses (the shared range fixture).
- **SC-005**: Zero axe violations on the dashboard and the Roof tab; nothing overlaps at 320 and
  390 px.
- **SC-006**: The web UI grows by at most 10 KB gzipped (SIZE-01).

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **No Open or Close without fresh live data**, Close included. A close in bad weather is what the
   device's own automatic close is for; a person's button acting on stale state is the greater
   risk. Stop never depends on live data.
2. **The page predicts refusals and still shows the device's answer.** The prediction only
   disables; it never enables something the device would refuse, and the device's checks are
   unchanged.
3. **`busy` leaves the buttons usable** (it lasts a moment), **`restarting` disables Open and Close
   until live data is back** or 30 s pass, and **`hotspot` disables Open** from the status
   document's hotspot flag. Close is never disabled for the hotspot.
4. **The Stop bar is pinned to the bottom of every page while the roof moves or a close waits**, not
   in the header (DS-01). It stays while the data is stale if the roof was last seen moving.
5. **No confirmation dialogs.** Stop must be instant; Open and Close are single deliberate presses
   the device checks.
6. **The card warns that Stop calls off the automatic close** while it is under way; Stop stays
   enabled (spec 005's stand-down is a person's right).
7. **The Status card shows roof problems only** (a fault; an automatic close held, unable to move or
   halted), not the roof's state, which is on the Roof card (DS-08).
8. **Roof settings stay editable while the roof moves**; the tab says the save will be refused and
   the device refuses it, so nothing is lost by editing.
9. **The Hall scale draws the saved thresholds on the dashboard and the edited ones in Settings**, so
   calibration can be checked before saving.
10. **The N.I.N.A. API version is shown as fixed (v2)**, the only one the device supports.
11. **The owner's review of the UI is a bench check.** The constitution asks for the owner to review
    UI changes before they are built; the owner wasn't available, so the PR carries screenshots and
    a bench item for that review.

## Out of scope

- The demo's simulated roof and bench mode (011): the demo keeps its static closed roof, and the
  states it can't produce are tested on the components.
- Commissioning docs (012).
- Any change to the device's checks, the status document, MQTT or Home Assistant (008, 010).
