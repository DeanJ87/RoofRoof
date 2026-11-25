# Feature Specification: Honest Live State and Consistent Secondary Text

**Feature Branch**: `spec/028-live-state-and-consistency`

**Created**: 2026-11-25

**Status**: Draft

## Context

Using the web UI on a phone and on the bench showed five rough edges. None of them changes what the
roof does; all of them change how much a person can trust what the page shows.

1. **Before the first reading.** A page that loads slowly shows its controls and values before the
   live stream has delivered anything. Values from a previous connection stay on screen after the
   stream drops, and a socket that died while a phone slept looks live until the browser notices.
   The first retry waits 5 s.
2. **Two speeds.** The dashboard's status stream (`/ws/status`) sends every 2 s, plus at once when
   the roof's state or fault changes. The diagnostics stream (`/ws/diag`) sends every 2 s at rest
   and every 250 ms while the roof moves. While the roof moves, the diagnostics page updates eight
   times as often as the dashboard's position bar. Both streams already send a snapshot when a client
   connects, but the diagnostics page and the System page's diagnostics card fall back to the
   document they fetched when the page opened whenever the stream has nothing, so after a drop they
   show readings from the moment the page was opened.
3. **Secondary text.** Notes, tile subtitles, choice details, the wizard's "Why this matters", page
   intros and System page lines use seven different sizes (0.72 rem to 0.86 rem) and three colours,
   and a note under a reading row sits below the row's divider, so it reads as part of the next row.
4. **"?" tooltips.** The dashboard lays its cards out with transforms, so each card is its own
   stacking context. A tooltip on one card is painted under the next card, whatever its z-index.
   A tooltip inside a scrolling table is clipped by it.
5. **The setup card.** Spec 026 put the roof's seven setup stages on the dashboard as their own card
   until every one is done. On a roof that is almost set up it takes a third of the main page to say
   one thing is left.

## Clarifications

### Session 2026-11-25

Each answer takes the safest option the constitution allows; they are also listed under
"Assumptions decided for the owner".

- Q: What does a page show while it has no reading from the current connection? → A: The same
  waiting state on first load and after a drop: "Connecting" before any reading on this page,
  "Reconnecting" after one. Values from an earlier connection are not shown as current anywhere the
  live stream feeds them; Stop stays reachable (constitution VII).
- Q: What rate do both streams share? → A: One schedule for both: every 2 s at rest, every 500 ms
  while the roof is opening or closing, and the status stream also at once (at most every 250 ms) on
  a state, fault or phase change, as today. The diagnostics stream goes from 4 to 2 a second while
  moving; the dashboard from one every 2 s to 2 a second.
- Q: When is a quiet socket treated as dead? → A: After 5 s without a message (two and a half rest
  intervals): it is closed and reopened at once.
- Q: How fast is the first retry? → A: 1 s, then doubling to 30 s, with up to 1 s of jitter. When
  the page becomes visible again, comes back from the browser's page cache, or the network comes
  back, a closed or quiet socket reconnects at once.
- Q: Which text is "secondary"? → A: Notes, tile subtitles, choice-card details, the wizard's "Why
  this matters" body, page intro lines, the alerts list's message line and the System page's subtle
  lines. One size, line height and colour (tokens) for all of them; tone colours (warn, bad, ok)
  stay. The banner's detail line is not secondary text: it carries a safety state (DS-14).
- Q: Where does the one-line setup prompt go? → A: A note row in the Status card (DS-01 keeps the
  space above the cards clear; DS-08 puts a problem no other card shows there), naming the first
  stage left and linking to it.
- Q: How is the tooltip fixed in general? → A: The tip is shown in the browser's top layer (the
  popover feature), positioned beside its "?" and kept inside the window, so no card, table or
  dialog can cover or clip it. Browsers without the feature keep today's placement.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - No reading, no state (Priority: P1)

A person opens the dashboard on a phone over weak Wi-Fi. Until the first live reading arrives the
page says it is connecting; it never shows a roof state, a position or a value it hasn't received.
When the phone wakes from sleep, or the stream drops, the page says it is reconnecting and shows no
old reading as current until a new one arrives.

**Why this priority**: a roof controller that shows "Closed" while it doesn't know is worse than
one that says it doesn't know.

**Independent Test**: component tests give the dashboard, System and diagnostics pages no reading
(first load, then after a drop) and check the waiting state; hook tests drive a fake socket through
open, message, drop, quiet and wake.

**Acceptance Scenarios**:

1. **Given** the dashboard is loading and the stream has sent nothing, **When** the page renders,
   **Then** it shows "Connecting", the Roof card's state reads "Connecting" (not a roof state) and
   Open and Close are unavailable with "No live data".
2. **Given** the dashboard showed live readings, **When** the stream closes, **Then** the page
   shows "Reconnecting" instead of the old readings until the next reading arrives.
3. **Given** a socket that has sent nothing for 5 s, **When** the 5 s pass, **Then** the page closes
   it, shows "Reconnecting" and opens a new one at once.
4. **Given** a phone tab coming back to the foreground with a closed socket, **When** it becomes
   visible, **Then** it reconnects at once, without waiting for the back-off.
5. **Given** the diagnostics page lost its stream, **When** it is waiting, **Then** it shows
   "Reconnecting", never the document fetched when the page opened.

---

### User Story 2 - One live rate (Priority: P1)

While the roof moves, the dashboard's position and the diagnostics page update at the same rate,
and both start from a reading taken when they connect.

**Why this priority**: the dashboard is where a person watches a move.

**Independent Test**: native tests of the shared schedule; unit tests of the demo's schedule, which
both of its streams use.

**Acceptance Scenarios**:

1. **Given** the roof at rest, **When** a client is connected to either stream, **Then** it gets a
   message every 2 s (±loop jitter).
2. **Given** the roof opening, **When** a client is connected to either stream, **Then** it gets a
   message every 500 ms.
3. **Given** a new client, **When** it connects to either stream, **Then** its first message is a
   reading taken at that moment.

---

### User Story 3 - Quiet, consistent secondary text (Priority: P2)

Every secondary line in the app (notes, tile subtitles, choice details, the wizard's "why") uses
one size, line height and colour, and a note under a reading row sits with that row.

**Independent Test**: a unit test reads the computed font size of each secondary class in a built
page; a Playwright check measures that a note after a reading row is above the row's divider.

**Acceptance Scenarios**:

1. **Given** any page or wizard screen, **When** it shows secondary text, **Then** it is at the
   secondary size and colour (tone colours excepted).
2. **Given** a reading row followed by a note, **When** it renders, **Then** the divider is under
   the note, not between the row and its note.

---

### User Story 4 - Tooltips on top (Priority: P2)

Hovering, focusing or tapping a "?" shows its tip above every card, table and panel, inside the
window.

**Independent Test**: Playwright hovers the Status card's "?" on the dashboard and checks the tip
is in the top layer and inside the window.

**Acceptance Scenarios**:

1. **Given** the dashboard at desktop width, **When** the Status card's "?" is hovered, **Then** the
   tip is drawn in the top layer, over every card.
2. **Given** a "?" near the window's edge, **When** its tip shows, **Then** the tip is inside the
   window.
3. **Given** a tip is open, **When** Escape is pressed or focus leaves, **Then** it hides (WCAG
   1.4.13), as today.

### User Story 5 - One line for an unfinished setup (Priority: P2)

While the roof's setup isn't finished, the dashboard says so in one line in the Status card, "Finish
setup: Weather check" with how many stages are left, and the line opens the first unfinished stage.
The full stages list is in Settings → Roof only.

**Independent Test**: unit test of the Status card's items for a roof with only the weather source
left; Playwright opens the demo's dashboard, finds the row and follows it.

**Acceptance Scenarios**:

1. **Given** every stage done but the weather source, **When** the dashboard shows, **Then** the
   Status card has one row "Finish setup: Weather check" with "1 left", linking to Settings → Roof's
   weather sources, and no stages card.
2. **Given** every stage done, **When** the dashboard shows, **Then** there is no setup row.
3. **Given** a stage the test box bypasses, **When** the dashboard shows, **Then** it isn't named in
   the setup row (the Test box row covers it).

### Edge Cases

- A stream that closes before its first message: still "Connecting" (nothing was shown).
- The diagnostics stream is closed by the device while diagnostics are off: the page shows the
  "off" note, as today.
- Two components share one socket: both switch to the waiting state together.
- The Stop bar: with no reading from the current connection the roof's state is unknown, so the
  Stop bar shows (the existing rule for no status), also after a drop.
- A browser without the popover feature: the tip keeps its in-card placement.
- Many tabs reconnecting at once after the device restarts: the back-off and jitter spread them;
  the device's per-endpoint cap (3) still replaces the oldest.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The live-stream hook MUST hand components only readings received on the current
  connection; when the connection closes, or is found dead, components get no reading until the next
  one arrives.
- **FR-002**: The hook MUST tell components whether they are connecting (no reading yet on this
  page) or reconnecting (a reading was received before).
- **FR-003**: A socket that has delivered nothing for 5 s MUST be closed and reopened at once.
- **FR-004**: Reconnect back-off MUST start at 1 s, double to at most 30 s and add up to 1 s of
  jitter; a closed or quiet socket MUST reconnect at once when the page becomes visible, is
  restored from the page cache or the browser reports the network back.
- **FR-005**: The dashboard, System page and diagnostics page MUST show one waiting state
  ("Connecting" or "Reconnecting") while they have no reading; the Roof card's state pill MUST read
  "Connecting" while no status has arrived, and Stop stays available.
- **FR-006**: The diagnostics page and the System page's diagnostics card MUST NOT fall back to the
  document fetched at page load once the stream has connected and dropped.
- **FR-007**: Both live streams MUST follow one schedule defined in pure logic: 2 s at rest, 500 ms
  while the roof is opening or closing. The status stream keeps its immediate send on a state, fault
  or close-phase change (at most every 250 ms). Both send a reading on connect. The demo follows the
  same schedule.
- **FR-008**: The schedule MUST keep within the connection budget: no change to the number of
  sockets per endpoint (3), the ping, or the stalled-client rule.
- **FR-009**: Secondary text MUST take its size, line height and colour from shared tokens, with one
  class for text that isn't a `Note`; the old one-off sizes for those texts are removed.
- **FR-010**: A note directly after a reading row MUST read as part of that row (the divider moves
  under the note).
- **FR-011**: "?" tooltips MUST be drawn above every other part of the page and kept inside the
  window, where the browser supports the top layer; keyboard, Escape and screen-reader behaviour stay
  as they are.
- **FR-012**: The device web bundle MUST NOT grow in LittleFS blocks, and the gzipped JS + CSS MUST
  NOT grow.
- **FR-014**: The dashboard MUST NOT show the setup stages card; while a stage is to do (or typed,
  not measured), the Status card MUST show one note row naming the first such stage, how many are
  left, what that stage needs behind "?", and a link to the page that does it. Settings → Roof keeps
  the full list.
- **FR-013**: The wizard's observatory-branch browser test MUST pin the demo's clock to a night time,
  so the daylight check doesn't depend on when it runs.

### Key Entities

- **Live phase**: connecting, live, reconnecting (per shared socket).
- **Live schedule**: rest interval, moving interval, minimum change interval.
- **Secondary text tokens**: size, line height, colour.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (cards), DS-04 (notes report a state; explanations behind "?"), DS-10 (sizes
  from tokens: this spec adds the secondary text tokens), DS-13 (empty state), DS-22, DS-23.
  A new rule, DS-15, names the secondary text tokens and class.
- **Mockup**: none; the changes restyle existing components only (no new component or layout).
  Screens are listed for the owner to check in the PR.
- **New strings**: `live.connecting` (title, and the Roof card's pill before the first status,
  "Connecting"), `live.reconnecting` (title, "Reconnecting"); `move.clears.live` reworded (note,
  "Clears when a live reading arrives."). To keep the bundle from growing, strings with the same text
  are shared (the "Could not reach the device" messages, five settings-dependency labels, the
  Diagnostics card's title and button, the wizard's no-alert-channel note).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With no reading, no roof state or reading appears on the dashboard, System or
  diagnostics pages (component tests).
- **SC-002**: While the roof moves, the dashboard and diagnostics streams are each due every 500 ms,
  and at rest every 2 s, on the device (native tests) and in the demo (unit tests).
- **SC-003**: After a drop, a visible page shows a fresh reading within 2 s of the device being
  reachable again (1 s first retry plus jitter).
- **SC-004**: Every secondary text class computes to the same font size.
- **SC-005**: The Status card's tooltip is in the top layer, over the neighbouring cards, and inside the window (Playwright).
- **SC-007**: With one stage left, the dashboard spends one Status row on setup, not a card.
- **SC-006**: The device bundle's gzipped JS + CSS is no larger than main's, and LittleFS use stays at
  or under 75%.

## Assumptions decided for the owner

1. **One rate, 500 ms while moving.** Both streams send every 500 ms while the roof moves. The status
   document is about 4 KB; at 4 a second to three clients that would be about 48 KB/s on the
   controller's Wi-Fi during a move. 2 a second halves it, and the position bar's movement between
   messages is about 19 mm at 38 mm/s. The diagnostics page's live chart has half the points it had;
   the recorded profiles (downloads) are unchanged.
2. **Old readings are hidden, not greyed.** After a drop the dashboard shows the waiting state instead
   of greyed old values (SQMeter's spec 010 greyed them). The Stop bar shows while there is no reading,
   as it already does when no status has arrived.
3. **Dead after 5 s quiet.** The rest interval is 2 s, so two messages missed in a row; a phone that
   wakes reconnects at once instead of after the 5 s back-off.
4. **First retry 1 s.** Up from SQMeter's 5 s; the device's per-endpoint cap and the doubling keep many
   tabs from crowding the connection budget.
5. **The Status card's "Data: Offline / No updates" row is gone.** With no reading the whole page
   says Connecting or Reconnecting, so the row could no longer show; removing it (and its strings)
   pays for this spec's code in the bundle.
6. **Banner detail unchanged.** The banner's detail line keeps its size: it carries a safety state on
   every page (DS-14).
7. **Top layer for tooltips.** The popover feature (all current browsers) is used instead of moving the
   tip in the DOM; older browsers keep the in-card tip.

8. **Finish setup lives in the Status card.** DS-01 keeps anything but the Arrange toolbar out of
   the space above the cards, and DS-08 makes the Status card the place for a problem no other card
   shows, so the one-line prompt is a Status row (a note, amber) rather than a strip or a new card. It
   counts towards "N to check" until setup is finished.

## Out of scope

- The Roof card's buttons, the Stop bar's own rules and the open confirmation (spec 027).
- Any change to what the streams contain, to the per-endpoint socket cap or to MQTT rates.
- A light theme or new colours.
