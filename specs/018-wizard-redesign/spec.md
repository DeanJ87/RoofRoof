# Feature Specification: Setup Wizard Redesign

**Feature Branch**: `spec/018-wizard-redesign`

**Created**: 2026-11-11

**Status**: Draft

## Context

Spec 015 built the setup wizard as an ordinary page of the app: a row of eleven page tabs, then
each page's Settings cards, often several to a page, with Save, Save and continue, Skip page and
Skip setup in a row of buttons at the bottom. It does its job, but a new owner sees long forms that
look like Settings, and has to read a whole card to find the one thing the page wants.

This spec keeps everything spec 015 guarantees and changes how the wizard is presented. It becomes
an onboarding flow over the app:

- a dialog over the dashboard, with the page behind it dimmed and blurred;
- a progress bar across the top, with Back on the left and Close on the right;
- one question per screen, answered with large selectable choices (an icon, a title, a line of
  detail, and a "Recommended" badge where there is a sensible recommendation), or with a few
  compact fields beside the live readings they need;
- the question asked in a speech bubble beside a small dog, RoofRoof's mascot (the name is a dog
  joke);
- a Continue button that stays at the bottom, unavailable (with its reason) until the screen is
  answered, and Skip for now beside it where a screen may be skipped;
- on a wide screen, a side panel that says why the question matters.

The long pages are split into short screens: the eleven pages of spec 015 become about twenty
screens, grouped under the same pages, so the saved resume point (`setup.step`, a page) keeps its
meaning. The flow ends on the review, then a quiet "done" screen linking the commissioning guide.

The new dialog, progress bar, choice card, speech bubble and mascot are shared design-system
components, documented with the others; constitution VII is amended so the design system allows
them (see "Constitution amendment").

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: What shows behind the dialog? → A: The dashboard, made inert (it can't be focused or
  clicked, and screen readers skip it while the dialog is open). The wizard keeps its own address
  (`/wizard`), so the System page's "Run setup wizard", the banner and the demo open it as before.
- Q: Where is the Stop bar while the dialog is open? → A: Inside the dialog, pinned to the bottom
  of the window above everything else, so it is visible, inside the focus loop and read by screen
  readers while the rest of the app is hidden from them. The dialog's own footer sits above it.
- Q: What do Close and Escape do? → A: Neither leaves at once. Both open a "Leave setup?" screen
  in the dialog with three choices: Keep going (the default, focused first), Leave for now (back to
  where the wizard was opened from; nothing more is saved, and the dashboard banner offers to carry
  on), and Skip setup (spec 015's warning, then setup is done). Escape on that screen is Keep
  going.
- Q: Is a choice already selected when a screen opens? → A: On a new device (setup state `new`), a
  screen not reached before shows no choice selected, and Continue waits for one: the owner says
  what is fitted rather than accepting a factory default unseen. On a device set up before (state
  `offered` or `done`), or a screen already passed, the current setting is selected. The password
  screen keeps spec 015's rule: protection starts selected when none is set.
- Q: What happened to the page tabs? → A: They go. The progress bar shows how far along the owner
  is; Back steps through the screens; the review's "Change" on each check jumps to its screen.
- Q: Which choices are "Recommended"? → A: Only policy choices with a default the earlier specs
  settled: a password (constitution VIII), Alpaca on (N.I.N.A. opens and closes the roof over the
  Alpaca Dome, spec 014), and daylight "Alert" (spec 013's default). Hardware facts (park switches,
  current sense, rain contact, Hall sensor) and alert channels have no recommendation: the right
  answer is what is fitted or used.
- Q: Light and dark? → A: RoofRoof has one theme, the dark night theme, whatever the browser
  prefers: a phone in light mode at the observatory must not light up the operator's eyes. The page
  declares itself dark so the browser's own controls (scrollbars, form widgets) follow. The new
  components use only the design tokens, so a future light theme changes them with the rest.
  Every wizard screen is tested with the browser set to light and to dark.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | Settings that change motion are refused while the roof moves | FR-012: every save is spec 015's save through the settings API, unchanged |
| I, III | The roof never moves by itself; the wizard moves nothing | FR-013: no screen has Open, Close, Clear fault or a motion test; the calibration screen links the attended page |
| VII | Stop is reachable on every page while the roof moves, never disabled | FR-014: the Stop bar is inside the dialog, above its content, in the focus loop |
| VII | A greyed-out control shows its reason | FR-006: an unavailable Continue says what the screen still needs |
| VII | DS components, no new button or card styles, phone width, WCAG 2.2 AA, i18n | FR-017 to FR-021 and the constitution amendment |
| VIII | Secrets stay masked; saves keep `requireAuth` | FR-012: the save path is unchanged |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A new owner answers one question at a time (Priority: P1)

The owner opens a newly joined RoofRoof. The dashboard dims and a dialog asks, beside a small dog,
"What should we call this roof?". Screen by screen it asks about the password, the site, the time
zone, the supply reading against a meter, whether current sense, park switches and a rain contact
are fitted (large choices, with the live state beside them), the Hall sensor, the calibration run,
the roof's travel, the weather source and its address with a Test, N.I.N.A.'s address with a Test,
Alpaca, an alert channel and its details with a test, and the daylight mode. A side panel on a
wide screen says why each matters. The review lists every check; Finish shows a quiet "You're set"
screen with the commissioning guide.

**Why this priority**: This is the redesign.

**Independent Test**: In the demo, restart the wizard from the Demo panel and walk every screen,
answering each; the settings end as entered, the review lists every check, Finish shows the done
screen, and "Go to dashboard" closes the dialog with no banner.

**Acceptance Scenarios**:

1. **Given** setup state `new`, **When** the dashboard opens, **Then** the dialog opens over the
   dimmed dashboard at the first screen of the saved page, with focus on the screen's question.
2. **Given** a choice screen not answered yet, **When** it opens, **Then** no choice is selected,
   Continue is unavailable and a note says to choose an answer; choosing one makes Continue
   available.
3. **Given** a screen with changes, **When** Continue is pressed, **Then** the changes are saved
   with the same validation as Settings and the next screen opens; a refused save keeps the screen
   with the device's reason.
4. **Given** the park switches screen, **When** "One" or "Two" is chosen, **Then** each fitted
   switch's live state shows beside the choices.
5. **Given** the review, **When** Finish is pressed, **Then** setup is done and the done screen
   links the commissioning guide and its gate for unattended use.

---

### User Story 2 - Leave, resume, skip, run again (Priority: P1)

The owner closes the dialog with the X (or Escape) half-way; the wizard asks whether to keep
going, leave for now or skip setup. Leaving returns to the dashboard with the "Setup isn't
finished" banner; the next page load opens the wizard at the page reached. Skip setup warns, then
ends setup. System → Run setup wizard opens it again at the first screen.

**Why this priority**: spec 015's guarantees; the owner must never be trapped or lose their place.

**Independent Test**: Continue past the first page, press Escape: "Leave setup?" with Keep going
focused; Leave for now shows the dashboard with the banner and focus on the page; reload: the
wizard opens at the second page.

**Acceptance Scenarios**:

1. **Given** the dialog open, **When** Escape or Close is pressed, **Then** the "Leave setup?"
   screen opens and nothing is saved or left yet; Escape again or Keep going returns to the screen.
2. **Given** "Leave for now", **When** pressed, **Then** the dialog closes to where it was opened
   from and focus returns to the control that opened it, or to the page when that control is
   gone.
3. **Given** "Skip setup", **When** pressed, **Then** spec 015's warning is shown and confirming
   sets setup done.
4. **Given** setup done, **When** System → Run setup wizard is pressed, **Then** the dialog opens
   at the first screen with the current settings selected.

---

### User Story 3 - Stop stays within reach (Priority: P1)

While the roof moves, the Stop bar shows above the dialog, can be reached with Tab and clicked,
and the screen says saves wait for the roof.

**Why this priority**: constitution VII; a dialog must never hide Stop.

**Independent Test**: In the demo, start an open, open the wizard: the Stop bar is visible and not
covered, Tab reaches it, clicking it stops the roof; Continue with a change is refused with the
device's reason.

**Acceptance Scenarios**:

1. **Given** the roof moving and the dialog open, **When** the page is inspected, **Then** the
   Stop button is visible, the topmost element at its centre, inside the dialog and enabled.
2. **Given** the same, **When** the owner tabs through the dialog, **Then** focus reaches Stop and
   wraps back to the dialog's first control.
3. **Given** the same, **When** Stop is clicked, **Then** the roof stops.

---

### User Story 4 - Usable by keyboard, screen reader and phone (Priority: P2)

The dialog is announced as a dialog named by its question; focus stays in it; the choices are a
radio group the arrow keys move through; the progress bar announces its value; nothing moves for a
reader who asked for reduced motion; at 320 px the dialog fills the screen with no sideways scroll.

**Why this priority**: constitution VII, WCAG 2.2 AA.

**Independent Test**: The accessibility scan passes on every screen at 320 px and desktop with the
browser set to light and to dark; keyboard tests cover the focus loop, Escape and the arrow keys.

**Acceptance Scenarios**:

1. **Given** a choice screen, **When** the arrow keys are pressed in the group, **Then** the
   selection moves and wraps, and Space or a click selects.
2. **Given** a screen change, **When** it happens, **Then** focus moves to the new question and
   the progress bar's value is the new screen's.
3. **Given** reduced motion, **When** screens change, **Then** nothing slides or fades.

---

### User Story 5 - An upgraded device is offered the wizard, not forced into it (Priority: P3)

Unchanged from spec 015: the banner offers it, nothing opens by itself, Dismiss ends it.

**Acceptance Scenarios**:

1. **Given** setup state `offered`, **When** the dashboard opens, **Then** the banner shows and
   the dialog doesn't open.

---

### Edge Cases

- The device is in hotspot mode: the dialog doesn't open by itself (spec 015).
- A conditional screen's condition changes (the Hall sensor marked not fitted, no weather source
  chosen, alerts off): the screens that depend on it are left out, and the progress bar counts
  only the screens that will show.
- The roof starts moving while the dialog is open: the Stop bar appears inside the dialog and the
  dialog's footer moves up above it; the screen's motion note says saves wait.
- The readings can't be read: "No reading", never a passing state (spec 015).
- The browser doesn't blur behind a dialog: the backdrop is a solid dim instead.
- A link out of the wizard (the calibration page, a Settings fix link, the docs): the calibration
  page and Settings close the dialog as Leave for now does; docs open in a new tab.
- The window is resized from phone to desktop width: the side panel appears beside the screen; on a
  phone the "why" text sits under the screen's content.
- Escape inside a "?" tip closes the tip only.

## Requirements *(mandatory)*

### Functional Requirements

**The dialog**

- **FR-001**: The wizard MUST open as a modal dialog over the app at `/wizard` (and `?step=N` for
  page N, as spec 015; `?screen=<id>` for one screen), with the rest of the app inert behind a
  dimmed, blurred backdrop (a solid dim where the browser can't blur).
- **FR-002**: The dialog MUST have a progress bar with its value (the screen's position among the
  screens that will show), Back (absent on the first screen) and Close, a heading that names the
  dialog, the screen's content, and a footer that stays in view with Continue and, where allowed,
  Skip for now.
- **FR-003**: Focus MUST stay within the dialog (Tab and Shift+Tab wrap), move to the screen's
  question on every screen change, and return on close to the control that opened the wizard (or
  to the page's main region when it no longer exists).
- **FR-004**: Escape and Close MUST open the "Leave setup?" screen (Keep going, Leave for now, Skip
  setup) and never leave directly; Escape on that screen MUST be Keep going.

**Screens**

- **FR-005**: Each screen MUST ask one question, shown in a speech bubble beside the mascot, with
  either a group of choice cards or a few fields with the live readings beside them, and a short
  "why this matters" text (a side panel at 900 px and wider, under the content below that).
- **FR-006**: Continue MUST be unavailable until the screen is answered (a choice made, a required
  field filled), with a note saying what is missing; on every other screen it is available.
- **FR-007**: The screens, under spec 015's pages, MUST be:

  | Page | Screen | Kind | Settings |
  |---|---|---|---|
  | Device | Name | field | `deviceName` |
  | Device | Password | choice: on (Recommended) / off, then the fields | `auth.*` |
  | Site and time | Site | fields | `location.*` |
  | Site and time | Time | fields | `ntp.timezone`, `ntp.enabled`, `ntp.server*` |
  | Hardware | Supply | reading + meter field | `roof.minSupplyVolts` |
  | Hardware | Current sense | choice: fitted / not yet, then scaling | `roof.currentSense*` |
  | Hardware | Park switches | choice: none / one / two, live states | `park.switches` |
  | Hardware | Rain contact | choice: fitted / not fitted, live state | `safety.rainContact` |
  | Hall sensor | Fitted? | choice: fitted / not fitted yet | (wizard only) |
  | Hall sensor | Thresholds (only if fitted) | fields + live scale | `roof.hall*` |
  | Calibration run | Calibrate | link to the attended page | none |
  | Roof motion | Travel | fields | `roof.travelMm`, `roof.speedMmPerSecond`, `roof.topDutyPercent` |
  | Weather | Source | choice: SQMeter over HTTP / MQTT / both | `safety.http.enabled`, `safety.mqtt.enabled` |
  | Weather | Address (only with a source) | fields + Test | `safety.http.url`, `safety.mqtt.topic` |
  | N.I.N.A. | Address | field + Test | `park.nina.url` |
  | N.I.N.A. | Alpaca | choice: on (Recommended) / off | `alpaca.enabled` |
  | Alerts | Channel | choice: ntfy / Pushover / webhook / MQTT | `alerts.enabled`, `alerts.<channel>.enabled` |
  | Alerts | Details (only with alerts on) | the channels' fields + Send test | `alerts.<channel>.*` |
  | Daylight | Mode | choice: off / alert (Recommended) / alert and close, then the threshold | `daylight.*` |
  | Review | Review | the checks, Finish | `setup` |
  | (after Finish) | Done | the commissioning guide | none |

- **FR-008**: The content rules of spec 015 FR-008 MUST still hold on the new screens (the password
  offered and warned, the meter compared within 0.2 V and never saved, the live Hall scale, "Not
  fitted yet" saving nothing and linking the quick bench test, "Use first-run values", the
  N.I.N.A. failsafe text and the Alpaca warning, the no-channel warning, the daylight modes
  explained).
- **FR-009**: Choice selection MUST follow the clarification: unselected on a new device's unseen
  screens, the current setting otherwise.

**Saving and moving**

- **FR-010**: Continue MUST save the screen when it has changes, through the settings API with the
  web UI's and the device's validation, with `setup.step` set to the page of the next screen;
  without changes it records the step only. Back and Skip for now MUST never save and MUST drop the
  screen's unsaved changes.
- **FR-011**: Tests MUST be spec 015's: the weather and N.I.N.A. Test save then read the device's
  own poll (`GET /api/roof/inputs`); Send test uses the saved channel.
- **FR-012**: Every save MUST be refused while the roof moves or a close waits (the device's
  existing rule), the screen MUST say so, and a refused save MUST keep the screen open with the
  reason.
- **FR-013**: No screen MUST offer Open, Close, Clear fault, Stop-as-a-test or any other roof
  command; the calibration screen links to the attended calibration page, which the dialog leaves
  for.
- **FR-014**: While the roof moves (spec 009's rule for showing Stop), the Stop bar MUST be inside
  the dialog, visible above all of it, enabled, reached by Tab, and announced; the dialog's footer
  MUST stay clear of it.
- **FR-015**: Resume, run again, the banner, Dismiss and "never from the hotspot" MUST behave as in
  spec 015.
- **FR-016**: The done screen MUST say setup is finished, offer a restart when a saved change needs
  one (Alpaca), and link the commissioning guide and its gate for unattended use.

**Design system and accessibility**

- **FR-017**: The dialog, the progress bar, the choice card group, the speech bubble and the mascot
  MUST be shared components in the design system's component file, documented in the coding
  standard's DS rules, built only from design tokens, and used by the wizard; no other new button
  or card style is added.
- **FR-018**: The dialog MUST be announced as a modal dialog named by the screen's question; the
  choices MUST be a radio group with arrow keys that move and wrap; the progress bar MUST expose
  its value and a text such as "Screen 3 of 20".
- **FR-019**: With reduced motion requested, nothing in the dialog MUST move or fade.
- **FR-020**: The mascot MUST be an original, simple dog drawn as a small inline image (under 3 KB),
  coloured from the design tokens, hidden from assistive technology.
- **FR-021**: Every string MUST go through i18n with a context note, and copy MUST meet DS-20 to
  DS-28 (except the questions and bubble prompts, which are full sentences; see the amendment).
- **FR-022**: Every screen MUST pass the accessibility scan with no violation at 320 px and
  1280 px, with the browser preferring light and dark, and fit 320 px without sideways scroll.

**Demo and docs**

- **FR-023**: The demo's "Restart setup wizard" MUST open the new dialog; the docs (setup wizard
  guide, first boot, coding standard's DS rules) and the screenshots MUST show it.

### Key Entities

- **Screen**: an id, the page it belongs to, its question, its "why", whether it shows (a
  condition on the settings), whether it is answered, whether it may be skipped.
- **Setup state**: unchanged (`new`, `offered`, `done`; the page to resume at).

## Design *(mandatory for UI changes)*

- **Look**: RoofRoof's dark tokens. The backdrop dims the app to near-black and blurs it; the
  dialog is a `--panel` surface with the card radius. The selected choice has a cyan edge, a
  tinted surface and a check mark (never colour alone). The mascot is a moonlit, round-faced dog
  with floppy ears and a small star tag on its collar, in token colours. One motion: the screen
  content fades in on a change (none with reduced motion).
- **Phone**: the dialog fills the screen; the footer is pinned to the bottom (above the Stop bar
  when it shows); the "why" text sits under the content.
- **Desktop**: a centred dialog up to 960 px wide; the content column and a 280 px "Why this
  matters" panel.
- **DS rules**: DS-02 (pills for the review states and the Recommended badge), DS-03 (live
  readings are `ReadingRow`s), DS-04 (field hints behind "?"), DS-06 (Tests in the screen that
  started them), DS-09 and DS-10 (tokens), DS-20 to DS-28 (copy), and the new DS rules for the
  onboarding components.
- **Mockup**: not reviewed. The owner reviews the screenshots in the pull request and the demo
  before merging (listed under the PR's pending checks).

## Constitution amendment

Principle VII says "no new button or card styles". A choice card is a new card style, and the
dialog is a new surface. Building the onboarding flow out of `SettingsCard`s and toggles is what
spec 015 did and what this spec replaces. So this spec amends VII (MINOR, 2.0.0 → 2.1.0: the
principle is expanded, nothing is relaxed): the design system gains one documented set of
onboarding components (the modal dialog, the progress bar, the choice card group, the speech
bubble and the mascot) in `ui.tsx`, usable by any guided flow; beyond them the rule stands. The
amendment also states that any modal over the app carries the Stop bar inside it, so the new
surface can't hide Stop. The questions and prompts in the bubble are full sentences, which DS-20's
three-word labels don't fit; they get their own copy rule (one question, at most 60 characters).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every screen asks exactly one question and has at most one choice group or at most
  four fields (checked by a test over the screen list).
- **SC-002**: The accessibility scan finds no violation on any screen at 320 px and 1280 px, with
  light and dark browser preferences (4 runs per screen).
- **SC-003**: With the roof moving, the Stop button is the topmost element at its centre on every
  screen tried, is reached by Tab within the dialog, and stops the roof when clicked.
- **SC-004**: Every spec 015 Playwright and Vitest guarantee (saves, refusal while moving, Test
  freshness, resume, skip, run again, banner, no roof command on any screen) passes against the new
  dialog.
- **SC-005**: The web UI grows by at most 6 KB gzipped (SIZE-01's limit is 10 KB), recorded in the
  size baseline with its reason.
- **SC-006**: The full local check passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **The dashboard is the backdrop**, inert while the dialog is open; the wizard keeps `/wizard`.
2. **Stop lives inside the dialog** while the roof moves, so it is in the focus loop and visible
   to screen readers that the dialog hides the rest of the app from.
3. **Close and Escape ask first** (Keep going, Leave for now, Skip setup). Leaving keeps the saved
   progress; only Skip setup ends setup, after spec 015's warning.
4. **No answer is assumed on a new device**: unseen choice screens start unselected so the owner
   states what is fitted. Re-runs show the current settings.
5. **The page tabs go**; Back, the progress bar and the review's Change links replace them.
6. **Recommended only where a spec settled the default**: password on, Alpaca on, daylight Alert.
7. **One dark theme**: no light theme is added; light-preferring browsers get the night theme, and
   the tests prove every screen in both preferences.
8. **One alert channel is chosen on the Channel screen**, the one most owners need; the Details
   screen shows every channel that is on, so more can be added there or in Settings.
9. **Weather source "none" is not a choice**: an owner without SQMeter skips the screen; the review
   still marks the missing source as a problem.
10. **The constitution is amended (MINOR)** to admit the onboarding components and to require the
    Stop bar inside any modal; spec 015's rules otherwise stand.
11. **The UI mockup review is pending**: the owner reviews the screenshots in the PR and the demo
    before merging.

## Out of scope

- Any change to the device, its settings or its API; any new setting.
- A light theme for the app.
- Using the onboarding components elsewhere (empty states may reuse the mascot later).
