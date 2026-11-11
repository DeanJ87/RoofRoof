# Feature Specification: Daylight Failsafe

**Feature Branch**: `spec/013-daylight-failsafe`

**Created**: 2026-11-11

**Status**: Draft

## Context

The imaging app's sequence (N.I.N.A.) opens the roof at dusk and closes it at dawn. RoofRoof does
not run the night; it is the failsafe behind the sequence. Today it steps in for one reason only:
the weather verdict stops being safe (spec 005), and it then asks N.I.N.A. to park, closes once
the park is confirmed and raises critical alerts when a step runs out of time.

Nothing watches the sun. If the sequence crashes, the imaging PC hangs or a person forgets, the
roof can stand open after sunrise on a clear morning: sunlight reaches the optics and the
equipment, and dew and dust follow during the day. The weather verdict stays safe, so spec 005
does nothing and no one is told.

Some owners point a telescope at the sun on purpose (white-light or H-alpha imaging with the right
filters). For them an open roof in daylight is normal, so the controller can't treat daylight as
a fault for everyone. This spec adds a daylight failsafe the owner sets to one of three modes:

| Mode | When the sun is up and the roof isn't closed |
|---|---|
| Off | Nothing. Solar imaging works as today. |
| Alert | An alert says the roof is open in daylight. Nothing moves. |
| Alert and close | The same close as for unsafe weather: ask N.I.N.A. to park, close only once the park is confirmed, never onto an unparked mount, with the same critical alerts when a step runs out of time. Opening is refused while the sun is up. |

"The sun is up" means its altitude, worked out on the device from the site's latitude and
longitude and the clock, is at or above a threshold the owner sets (in degrees). Unsafe weather
keeps every behaviour it has today.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows that keeps solar imaging possible,
and is also listed under "Assumptions decided for the owner".

- Q: Which mode is the default, also after an upgrade? → A: **Alert.** "Off" leaves the gap this
  spec closes. "Alert and close" would close the roof on owners who image the sun, the moment
  they upgrade, and the constitution forbids a migration that turns a motion feature on.
  "Alert" tells every owner about an open roof in daylight without moving anything; an owner
  who wants the close picks it once.
- Q: What altitude counts as "the sun is up", and how does it avoid flickering at the threshold?
  → A: Default 0° (the sun's centre on the horizon), settable from -18° to +30°. The sun counts
  as up at or above the threshold and as down again only once it is 0.5° below it.
- Q: What happens when the latitude and longitude aren't set, or the clock isn't set? → A: The
  check can't tell day from night, so it does nothing: no alert about daylight, no close, no
  refusal. With the mode on (alert or alert and close), an alert "Daylight check not working"
  goes out once the check has been unable to run for 10 minutes (a clock set by NTP within that
  time after a restart never alerts). With the mode off, nothing.
- Q: With "alert and close", is opening refused while the sun is up? → A: Yes, from every source
  (web UI, Alpaca, MQTT, Home Assistant), with a reason naming the sun's altitude and the
  threshold, and an open under way when the sun comes up is stopped, both as unsafe weather does
  today. Otherwise an open in daylight would be followed at once by a park request and a close,
  fighting the person who opened it. An owner who wants the roof open in daylight sets the mode
  to off or alert; both never refuse anything.
- Q: Can the daylight close override a person's stop, or a close that was halted? → A: No. A
  person's stop stands the daylight close down exactly as it stands down the weather close, and a
  halted close stays halted. Both re-arm only once the sun has been down (or the mode not "alert
  and close") for 10 minutes, or a person closes the roof.
- Q: What if the weather turns unsafe while a daylight close is stood down, halted or backing off
  after a failure? → A: The weather close starts at once, as it would have without the daylight
  setting: the constitution requires unsafe weather to end closed, and that rule doesn't change
  because a person stopped an earlier, daylight-only close. A person can stand the weather close
  down again with Stop.
- Q: And a weather close stood down by a person, after the weather clears in daylight with "alert
  and close"? → A: It stays stood down while the sun is up (no close against the person's stop),
  re-arming once the sun has been down for 10 minutes. Without the daylight close it would have
  re-armed 10 minutes after the weather cleared and then done nothing; staying stood down is the
  same for the roof and keeps the reminder going while the roof is open in daylight.
- Q: Which alert events does daylight get? → A: Three new events in spec 007's design, each with a
  level, sound and wording in Settings → Alerts: **Roof open in daylight** (the sun is up, the
  roof isn't closed and the mode is on; says whether it is being closed), **Roof closed after
  daylight** (a daylight close reached closed), and **Daylight check not working**. A daylight
  close that can't finish raises the existing critical events (mount didn't park, roof didn't
  close) with their guarantees, worded for daylight.
- Q: Is "Roof open in daylight" critical? → A: No. It goes through the usual rules (alerts on, not
  paused, its level not off, the cooldown), with a default level of "urgent". With "alert and
  close" the critical events cover every way the close can fail.
- Q: Does "Roof open in daylight" also fire while the weather close is already closing the roof?
  → A: No. "Unsafe: closing" has already said the roof is being closed; a second alert for the
  same close would be noise.
- Q: Where do the latitude and longitude come from? → A: Two settings, as SQMeter has them
  (`location.set`, `location.latitude`, `location.longitude`), entered in Settings → Roof. RoofRoof
  has no GPS.
- Q: Where is daylight shown? → A: The Roof card shows a daylight line (the mode, the sun's
  altitude against the threshold, or why the check can't run), and Open's reason says when
  daylight refuses it. Settings → Roof has a Daylight card. The status document carries a
  `roof.daylight` object. The demo panel gets a "Sun up" switch.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | The roof never moves by itself while its position is unknown | FR-008: the daylight close is spec 005's sequence, which never moves with the position unknown or a fault latched |
| II | Close only onto a confirmed park; park not confirmed: stay open and raise a critical alert | FR-008, FR-009: the daylight close asks for the park and closes through spec 003's park gate; a park not confirmed in time raises "mount didn't park" |
| II | Every close request is checked the same way, whatever its source | FR-008: the automatic close passes the same gate whatever started it |
| III | Unsafe or raining always ends parked and closed | FR-012, FR-013: the weather close is unchanged and a daylight stand-down never holds it back |
| III | A timed-out step stops where it is and raises a critical alert naming the step | FR-009 |
| III | The roof never opens by itself | FR-010: nothing in this spec opens; daylight can only refuse or stop an open |
| III | Every refusal states the value and the limit, or the input that failed | FR-005, FR-011: "Sun 3.2° ≥ 0.0°", "Location not set", "Clock not set" |
| III | Alerts, MQTT and the web UI can't change the verdict or the motion decisions | FR-018: the daylight state is computed on the device; nothing a client sends changes it except the saved settings |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Told when the roof is open after sunrise (Priority: P1)

An owner's imaging PC crashed at 3 a.m. and the sequence never closed the roof. With the default
mode (alert), the owner's phone gets "Roof open in daylight" shortly after sunrise, with the sun's
altitude, and goes out to close it.

**Why this priority**: This is the default for every owner, solar imagers included, and it closes
the silent gap with no motion.

**Independent Test**: Set the location and the clock, set the mode to alert and the threshold so
the sun is above it, leave the roof open with the weather safe: exactly one "Roof open in
daylight" goes out, nothing moves and opening is still allowed. Close the roof, and the next
sunrise with the roof open alerts again.

**Acceptance Scenarios**:

1. **Given** mode alert, the location set and the clock set, **When** the sun reaches the
   threshold with the roof open, **Then** "Roof open in daylight" goes out once with the sun's
   altitude and the threshold, and nothing moves.
2. **Given** that alert went out, **When** the roof stays open, **Then** no further "Roof open in
   daylight" goes out until the roof has been closed or the sun has set and the condition starts
   again.
3. **Given** mode alert and the sun up, **When** a person opens the roof, **Then** the open is
   accepted and "Roof open in daylight" goes out (after the start-up grace and cooldown rules).
4. **Given** mode off, **When** the sun is up and the roof open, **Then** nothing is sent and
   nothing moves.

---

### User Story 2 - The roof closes itself after sunrise (Priority: P1)

An owner who never images the sun sets "alert and close". When the roof is still open at sunrise,
the controller asks N.I.N.A. to park the mount, waits for the park to be confirmed, closes the
roof and says so. If the mount doesn't park in time, the roof stays open and the owner gets the
critical "mount didn't park" alert.

**Why this priority**: It turns a forgotten roof into a closed one with no person needed, under
the same park rules as the weather close.

**Independent Test**: In the demo or the native tests, with "alert and close" and the sun up and
the weather safe, open the roof: the sequence requests a park, closes after the park holds and
sends "Roof open in daylight" (closing) and "Roof closed after daylight". With the park never
confirmed: "Mount didn't park" (critical) at the park time limit and the roof stays open.

**Acceptance Scenarios**:

1. **Given** "alert and close", the sun up, the weather safe and the roof open, **When** the
   sequence runs, **Then** it asks N.I.N.A. to park, closes once the park has held, and the roof
   ends closed; "Roof open in daylight" and "Roof closed after daylight" go out.
2. **Given** the same, **When** the park isn't confirmed within `park.timeoutSeconds`, **Then**
   the roof stays open and the critical "mount didn't park" goes out, worded for daylight.
3. **Given** a daylight close under way, **When** a person presses Stop, **Then** the roof stops,
   the sequence stands down (critical "automatic close stopped", worded for daylight), and it
   doesn't close by itself again while the sun is up.
4. **Given** "alert and close" and the sun up with the roof closed, **When** anyone tries to open
   it (web UI, Alpaca, MQTT, Home Assistant), **Then** the open is refused with a reason naming
   the sun's altitude and the threshold.
5. **Given** "alert and close" and the sun up, **When** the mode is changed to alert or off,
   **Then** opening is allowed again and an attempt still waiting for the park ends without a
   close.

---

### User Story 3 - Unsafe weather is handled exactly as before (Priority: P1)

Whatever the daylight setting, rain at dawn ends with the mount parked and the roof closed, as
spec 005 does today, with the same alerts.

**Why this priority**: Constitution III. The daylight failsafe must never weaken the weather one.

**Independent Test**: Every existing close-sequence test passes unchanged. With the mode off or
alert, a randomised run of weather, park, roof and sun inputs produces exactly the same actions
and events as the sequence without daylight. With a daylight close stood down by a person, the
weather turning unsafe starts the weather close at once.

**Acceptance Scenarios**:

1. **Given** mode off or alert, **When** the weather turns unsafe, **Then** the sequence acts
   exactly as spec 005 specifies (same actions, events, alerts and timings).
2. **Given** "alert and close" with a daylight close stood down by a person, **When** the weather
   turns unsafe, **Then** the weather close starts at once ("Unsafe: closing") and runs to closed.
3. **Given** a weather close under way, **When** the sun also comes up, **Then** the close carries
   on unchanged and no "Roof open in daylight" goes out.

---

### User Story 4 - Told when the daylight check can't work (Priority: P2)

An owner turned the daylight setting on but never entered the location, or the device's clock
was never set. The owner is told the check can't run, instead of trusting a failsafe that isn't
there.

**Why this priority**: A silent failsafe that doesn't work is worse than none.

**Independent Test**: With mode alert and no location, after 10 minutes "Daylight check not
working" goes out naming the location; with the location set and no clock, it names the clock;
with the mode off, nothing.

**Acceptance Scenarios**:

1. **Given** mode alert or "alert and close" and the location not set, **When** 10 minutes pass,
   **Then** "Daylight check not working: Location not set" goes out once.
2. **Given** the clock not yet set after a restart, **When** NTP sets it within 10 minutes,
   **Then** no alert goes out.
3. **Given** the check can't run, **When** the mode is "alert and close", **Then** nothing is
   refused and nothing closes because of daylight.

---

### User Story 5 - Set it up and see it (Priority: P2)

The owner sets the mode, the threshold and the location in Settings → Roof, sees the sun's
altitude and the daylight state on the Roof card, sets the three new events' levels in Settings →
Alerts, and can try it all in the demo with a "Sun up" switch.

**Why this priority**: Without the settings and the display the feature can't be used or
trusted.

**Independent Test**: In the demo, turn on "Sun up" with the mode on "alert and close" and the
roof open: the Roof card shows daylight, the roof parks and closes, Open shows the daylight
reason.

**Acceptance Scenarios**:

1. **Given** Settings → Roof, **When** the owner picks a mode, a threshold and a location and
   saves, **Then** the device keeps them and the Roof card shows the new state within a status
   update.
2. **Given** a threshold or location out of range, **When** saving, **Then** the page and the
   device refuse it with the range.
3. **Given** the demo with "Sun up" on, **When** the mode is "alert and close" and the roof open,
   **Then** the demo roof parks and closes and Open is disabled with the daylight reason.

---

### Edge Cases

- The sun's altitude sits on the threshold for minutes (high latitudes): the 0.5° band keeps the
  state from flickering, so the alert and the close start once.
- The roof's position is unknown or a fault is latched at sunrise with "alert and close": the
  sequence can't move, raises "roof can't close by itself" (critical, daylight wording), as for
  unsafe weather.
- N.I.N.A. isn't set up and the mode is "alert and close": "mount didn't park" (N.I.N.A. not
  configured) at once, as for unsafe weather; the roof stays open.
- The device restarts during a daylight close: the attempt carries on after the restart with the
  time it had run, as spec 005 keeps a weather attempt, and a daylight stand-down survives a reset.
- The clock jumps (NTP sets it after a restart): the sun is worked out from the new time on the
  next pass; no state depends on the clock's history.
- Polar day or night: the altitude is simply above or below the threshold all day.
- The threshold is changed while the sun is up: the next pass uses the new threshold (with the
  band).
- The weather turns unsafe and safe again quickly while a daylight close is stood down: the
  weather close that started carries on as a close while the sun is up (daylight still wants the
  roof closed); the person can stop it again.
- The mode is set to "alert and close" mid-morning with the roof open: the close starts on the
  next pass, like the weather close after a verdict change.
- "Alert and close" while the location is unknown: nothing is refused, no close (FR-005).

## Requirements *(mandatory)*

### Functional Requirements

**Settings**

- **FR-001**: The device MUST keep `daylight.mode` (`off`, `alert` or `close`, the last shown as
  "Alert and close"), default `alert`, and `daylight.sunAltitudeDeg`, default 0, from -18 to +30
  degrees. An upgrade from a release without them MUST load with the defaults, never `close`.
- **FR-002**: The device MUST keep the site as SQMeter does: `location.set` (default false),
  `location.latitude` (-90 to 90) and `location.longitude` (-180 to 180, east positive). Values out
  of range are refused by the device and the web UI with the range.

**The daylight state**

- **FR-003**: About once a second, the device MUST work out the sun's altitude from the location
  and the clock with SQMeter's solar position (accurate to well under a degree), and decide one
  daylight state: `off` (mode off), `unknown` (mode on and the location not set or the clock not
  set), `night` or `day`.
- **FR-004**: The state MUST become `day` when the altitude is at or above the threshold and go
  back to `night` only when it is more than 0.5° below the threshold. A mode change or an unknown
  input resets the band (the next known altitude decides from scratch).
- **FR-005**: Every daylight text MUST state the value and the limit, or the input that failed:
  "Sun 3.2° ≥ 0.0°", "Sun -4.1° < 0.0°", "Location not set", "Clock not set".
- **FR-006**: The daylight state MUST NOT change the weather verdict, the park result or any
  other safety input; it is a separate input to the close sequence and the open check.

**Alert and close**

- **FR-007**: With mode `close` and the state `day`, the device MUST want the roof closed exactly
  as it does for unsafe weather. With any other mode or state, daylight wants nothing.
- **FR-008**: A close wanted for daylight MUST run through spec 005's close sequence unchanged:
  the park request, the confirmed park held for its settle time, the close through spec 003's
  park gate and interlock, every time limit, the back-off after a failure, the stand-down on a
  person's stop, the halt of a close that runs too long, the position-unknown and fault rules,
  and the record kept across a restart.
- **FR-009**: A daylight close that can't finish MUST raise the same critical events as the
  weather close (mount didn't park, roof didn't close, roof can't close by itself, automatic close
  stopped), with every guarantee spec 007 gives them, worded for daylight ("It is daylight" in
  place of "The weather is unsafe").
- **FR-010**: Nothing in this spec opens the roof.
- **FR-011**: With mode `close` and the state `day`, every open command MUST be refused (reason
  `daylight`, with the sun's altitude and the threshold) whatever its source, and an open under
  way MUST be stopped, as an unsafe verdict does. The weather refusal takes precedence when both
  apply. Modes off and alert never refuse or stop anything.
- **FR-011a**: As for unsafe weather (spec 006), an Alpaca or MQTT client's stop is refused
  (`unsafeClose`) while a close runs or waits and daylight wants the roof closed; a person's Stop
  is always accepted and stands the sequence down.

**Unsafe weather unchanged**

- **FR-012**: With mode off or alert, or the state not `day`, the close sequence MUST behave
  exactly as spec 005 specifies, step for step.
- **FR-013**: When the weather stops being safe during an episode the daylight close started and
  the sequence is halted, backing off after a failure or unable to move, the sequence MUST re-arm
  and start the weather close at once (an unable-to-move sequence raises its critical alert again,
  in the weather's words) (as if the daylight episode had not happened); when it is stood down by a
  person, once the weather has been not safe for 30 s in all since the stop, added up over unsafe
  spells (a new stop starts the count again). The critical alerts' throttle starts afresh at that
  moment, and also when a weather episode starts after a daylight-only episode ended. An
  attempt waiting for the park or a close under way carries on. From then on the episode is a
  weather episode (its alerts are worded for the weather).
- **FR-014**: A stand-down or halt MUST re-arm only after nothing has wanted the roof closed
  (neither the weather nor daylight) for 10 minutes, or the roof reads closed. Daylight never ends
  a stand-down or a halt.

**Alerts**

- **FR-015**: The device MUST raise three new events through spec 007's alert design (level,
  sound, title and message templates, a test send, the usual rules: master switch, pause, send
  mode, start-up grace, cooldown):
  - `daylight_open` "Roof open in daylight", default level urgent (3): rises when the mode is on,
    the state is `day`, the roof isn't closed and the weather close isn't running, for 30 s
    without a break; ends when any of these stops. Variables `reason` (the sun text) and
    `action` (what happens next: being closed, or nothing because the mode is alert only).
  - `daylight_closed` "Roof closed after daylight", default normal (2): the roof reached closed at
    the end of a daylight episode. Variable `reason` (the sun's text during the episode).
  - `daylight_unknown` "Daylight check not working", default normal (2): the mode is on and the
    state has been `unknown` for 10 minutes. Variable `reason` ("Location not set" or "Clock not
    set").
- **FR-016**: "Unsafe: closing" and "Roof closed after unsafe weather" MUST keep firing for
  weather episodes only, as today; a daylight-only episode raises the daylight events instead.
- **FR-017**: The three events MUST be in the settings-dependency catalogue (they depend on alerts
  on and the daylight mode on) and in the alert variables catalogue.

**Reporting and UI**

- **FR-018**: `/api/status` and `/ws/status` MUST carry `roof.daylight`: `mode`, `state`,
  `sunAltitudeDeg` (null when unknown), `limitDeg`, `text` and `closes` (true when daylight wants
  the roof closed now). Nothing a client sends changes the daylight state except saving the
  settings.
- **FR-019**: The Roof card MUST show a daylight line (state and text, or "off") and Open's
  reason MUST name daylight when it would be refused for it.
- **FR-020**: Settings → Roof MUST have a Daylight card with the mode, the threshold and the
  location (set, latitude, longitude), each with its "?" hint, and the threshold and location
  marked as unused while the mode is off.
- **FR-021**: Settings → Alerts MUST list the three events with the other roof events.
- **FR-022**: The demo MUST run the same daylight logic in its device core, and the demo panel
  MUST have a "Sun up" switch that puts the sun well above the threshold whatever the location
  and clock, so the alert and the close can be tried.
- **FR-023**: The docs MUST describe the modes, the threshold, the location, the unknown case,
  the refusal, the new events, the status object and the demo switch, in the user guide, the
  configuration and REST references and the commissioning steps.

### Key Entities

- **Daylight settings**: the mode, the threshold, and the site (set, latitude, longitude).
- **Daylight state**: off, unknown (with which input is missing), night or day; the sun's
  altitude; the threshold; whether daylight wants the roof closed now.
- **Daylight episode**: a close-sequence episode started by daylight alone. It becomes a weather
  episode once the weather is unsafe during it.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (a Daylight card in the Roof tab, built from the existing card and settings
  controls), DS-04 (explanations behind "?"), DS-08 (the Roof card shows state, not actions),
  DS-20 to DS-28 (labels, hints and notes within their limits). No new button or card styles
  (constitution VII).
- **Mockup**: not reviewed. The owner was not available; the Daylight card and the Roof card line
  reuse existing rows and controls only. The owner reviews the result in the demo before merging
  (listed under the PR's pending checks).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every spec 005, 007 and 011 native test passes unchanged.
- **SC-002**: A randomised test of at least 5,000 runs with the mode off or alert (sun up or
  down) shows the close sequence's actions and events identical, step by step, to a sequence fed
  the same weather, park and roof inputs without daylight.
- **SC-003**: A randomised test of at least 5,000 runs with "alert and close" never closes without
  the park confirmed, never sends an open, never ends a person's stand-down or a halt while the
  weather stays safe, and always starts the weather close within one step of the weather turning
  unsafe after a daylight stand-down.
- **SC-004**: The sun's altitude matches SQMeter's reference cases (within their 0.5°
  tolerance), and the threshold band is tested one second either side of both edges.
- **SC-005**: The open check refuses an open for daylight only with mode close and the state day,
  for every source, and the weather refusal wins when both apply.
- **SC-006**: The full local check (firmware build, native tests, web tests, Playwright, quality
  gate, docs build, flash and UI budgets) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows that keeps solar imaging
possible was taken.

1. **Default mode: alert.** Off leaves the silent gap; "alert and close" would start closing the
   roof on solar imagers at their first sunrise after the upgrade, and the constitution forbids a
   migration that turns a motion feature on. Alert moves nothing and tells everyone.
2. **Default threshold 0°, range -18° to +30°, 0.5° band.** The sun's centre on the horizon is the
   plain meaning of "after sunrise". A lower value closes earlier (useful for dawn sky glow); a
   higher one suits a site with a high horizon. The band stops a sun that lingers at the threshold
   from flickering the state.
3. **Location or clock unknown: no daylight action, an alert after 10 minutes.** Guessing day or
   night could close in the night or refuse every open; an alert that the failsafe isn't working
   is the honest answer. The 10 minutes let NTP set the clock after a restart without an alert.
4. **"Alert and close" refuses opening while the sun is up, and stops an open under way.** As
   unsafe weather does; otherwise the controller would fight the person who opened it. Solar
   imaging stays possible with the mode off or alert, which never refuse anything.
5. **Unsafe weather re-arms a daylight-only stand-down, halt or back-off.** The constitution
   requires unsafe weather to end closed; a person's stop of a daylight close is not a stop of a
   weather close that hadn't started. The person can stop the weather close too. A person's
   stand-down waits for 30 s of unsafe weather in all (added up over spells, so a flapping verdict
   still ends it) first, so one stale or missed SQMeter poll can't
   send a park request against the person's stop; halts and back-offs re-arm at once. The
   critical alerts start afresh then, so a weather failure isn't hidden by the same daylight
   failure minutes before.
6. **A weather stand-down stays while the sun is up with "alert and close".** It re-arms once the
   sun has been down for 10 minutes. The roof doesn't move either way; the reminder keeps going.
7. **"Roof open in daylight" is not critical** and defaults to urgent. The constitution's critical
   alerts are for unsafe weather and the park; the daylight close's failures still raise those
   critical events.
8. **No "Roof open in daylight" while the weather close is running**, which already said the roof
   is closing.
9. **The location uses SQMeter's settings names** (`location.set`, `latitude`, `longitude`,
   constitution XI) and sits in Settings → Roof next to the daylight mode, the only feature that
   uses it. No "use my browser's location" button.
10. **Status only**: the daylight state goes into the status document and the dashboard; MQTT
    state topics and Home Assistant entities are left for a later spec.
11. **The UI mockup review is pending**: the owner reviews the Daylight card and the Roof card line
    in the demo build before merging.
12. **No 30-minute reminders for a person's stop of a daylight close.** The first "Automatic
    close stopped" goes out; a person who stopped it meant it, and a wake-me reminder every 30
    minutes all day would be noise. The reminders come back while the weather itself is unsafe,
    and stay for halts, park failures and "can't close by itself", and for every weather episode.
13. **The daylight events follow the alert pause, including "only while an imaging app is
    connected".** A crashed imaging PC disconnects, so that setting also silences "Roof open in
    daylight"; the docs say to use "Any time" when relying on it. Exempting one non-critical event
    from the pause is left to the owner.
14. **A device with no site set says so**: after an upgrade, "Daylight check not working: Location
    not set" goes out once, 10 minutes after the restart, and Settings shows the daylight mode as
    not in effect until the site is set (or the mode is off).
15. **Accepted small gaps.** After a restart that loses the clock (rare: a soft reset keeps it), a
    person's daylight stand-down counts the unknown daylight state as "nothing wants a close" and
    re-arms after 10 minutes, so the close may start once NTP sets the clock with the sun up (still
    only onto a confirmed park). A weather stand-down kept by daylight still says it re-arms after
    10 minutes of safe weather. Downgrading to the previous release reads the new restart flag as
    a recent park request (the first request waits up to 30 s). Full-length custom texts in a
    multi-byte script for all six imaging-app and daylight events no longer fit the stored alert
    settings; the save is refused with a message.

## Out of scope

- MQTT topics and Home Assistant entities for daylight.
- Moon or twilight rules, horizon profiles, or any open by schedule (the roof never opens by
  itself).
- A GPS or browser-supplied location.
