# Feature Specification: Roof Alerts

**Feature Branch**: `spec/007-roof-alerts`

**Created**: 2026-11-11

**Status**: Draft

## Context

Spec 005 closes the roof when the weather turns unsafe and raises a critical alert when it can't:
the mount didn't park, the close failed, the roof can't close by itself, or a person stopped the
automatic close. It sends those alerts with fixed wording under one placeholder event (`roof`), at
the "wake" level, past the alert pause, the start-up grace and the alerts master switch, and it
sends one again (after a minute, doubling up to 30 minutes) until a channel has taken it.

The rest of the roof's life is silent: a move that faults, a 12 V supply that sags, a weather
source that stops answering, the roof starting to close because of the weather and the roof
reaching closed afterwards are only in the serial log.

This spec gives the roof its own alert events, following SQMeter's alert design (spec 008 there):
one row per event in Settings > Alerts with a level (off, quiet, normal, urgent, wake me), a
Pushover sound and optional wording with `{variables}`, a test send per event, and the same rules
for when an alert goes out. The roof events are:

| Event | When | Critical |
|---|---|---|
| Mount didn't park | spec 005: the park wasn't confirmed in time, or N.I.N.A. isn't set up | yes |
| Roof didn't close | spec 005: the close faulted, was refused, stopped part-way or took too long; the roof can't close by itself; a person stopped the automatic close | yes |
| Roof fault | a motion fault latched (a timeout, a jam, the wrong magnet, the supply, ...) | no |
| 12 V supply low | the supply has read below its minimum (or unreadable) for 30 s | no |
| Unsafe: closing | the automatic close started because the weather isn't safe | no |
| Roof closed after unsafe weather | the roof reached closed during an automatic close episode | no |
| Weather source lost | a weather source that is switched on has gone stale, unreachable, unreadable or offline for 30 s | no |

The two critical events keep every guarantee spec 005 gave them. They are the alerts constitution
II and III require, so a setting can make them louder or reword them, never silence them.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Which of spec 005's four critical events become which new event? → A: "Mount didn't park" is
  its own event. "Roof didn't close" covers the close that failed, the roof that can't close by
  itself (position unknown, fault latched, controller not started) and the automatic close stopped
  by a person: in each case the roof is open while unsafe and a person must act. The title and
  message still say which case it is (spec 005's wording).
- Q: Can a critical event be switched off or set quiet? → A: No. Its level is "urgent" or "wake
  me" (default "wake me"); the device and the web UI refuse anything lower, and the device treats
  a lower stored value as "urgent". Constitution II and III require the alert; "urgent" still
  breaks through Pushover's quiet hours.
- Q: Can a critical event have its own wording? → A: Yes, but the reason can't be lost: a custom
  message that uses neither `{reason}` nor `{details}` gets the built-in message added under it
  (constitution III: every alert states the value and the limit, or the input that failed). A
  reminder's title still starts with "Still: ".
- Q: Do the alert pause, the start-up grace and the master switch hold the critical events back?
  → A: No, as in spec 005: they go to every channel that is switched on, whatever the pause, grace
  or master switch say. The throttle (each event and cause at most every 30 minutes, with a
  30-minute reminder while the roof stays open) and the resend of an alert no channel took stay as
  they are.
- Q: And the other five events? → A: They follow the usual rules: nothing while alerts are off,
  paused (including "only while an imaging app is connected") or the event's level is off; nothing
  during the start-up grace (a condition still present when the grace ends is announced then);
  each event at most once per cooldown (`alerts.cooldownSeconds`), a change held back by the
  cooldown going out when it ends if the condition still holds. Several events raised in the same
  pass go out as one notification at the loudest level.
- Q: When does "roof fault" fire? → A: When a motion fault latches, any kind, whoever moved the
  roof. It fires again only after the fault has been cleared and another one latched. A fault
  latched during the automatic close also raises the critical "roof didn't close"; both go out
  (they are different events, and "roof fault" may be set to a level the owner hears).
- Q: Is a fault (or a low supply, or a lost source) that is already there at start-up announced?
  → A: Yes, once the start-up grace ends, unless it was already announced before a reset (kept in
  memory that survives a reset, not a power cut). So a power cut followed by a faulted roof is
  reported, and a board that keeps resetting doesn't repeat the same alert after every reset.
- Q: How low is "12 V supply low", and for how long? → A: Below the roof's existing minimum
  (`roof.minSupplyVolts`, the same limit that refuses a move), or unreadable, for 30 s without a
  break, so a motor's start-up dip doesn't alert. It fires again only after the supply has been
  back above the minimum.
- Q: What is "weather source lost"? → A: Any weather source that is switched on (SQMeter over HTTP,
  SQMeter over MQTT, the rain contact) going stale, unreachable, unreadable or offline, for 30 s
  without a break. Having no source switched on is a setting, not a loss, and doesn't fire (the
  verdict is unsafe then, and the Safety page says why). The start-up wait for a first reply is not
  a loss. It fires again only after every source is back.
- Q: When does "unsafe: closing" fire, and how often? → A: When the automatic close begins an
  attempt (waiting for the park, closing, or waiting to try again), at most once per cooldown. A
  verdict that flaps between safe and unsafe doesn't send one per flip. A sequence that can't move
  the roof at all (unknown position, fault) raises the critical "roof didn't close" instead.
- Q: When does "roof closed after unsafe weather" fire? → A: When the roof reaches closed while the
  automatic close is attempting a close (waiting for the park, closing, or waiting to retry),
  whoever closed it, at most once per cooldown. A closed magnet that flickers back to closed while
  the sequence can't move or is stood down doesn't count. If the weather clears before the close,
  nothing more is sent (the log says so).
- Q: What happens to the `roof` event name? → A: It is gone. Webhook and MQTT payloads now carry the
  new event names (`mount_not_parked`, `roof_not_closed`, ...). Spec 005 never shipped in a
  release, so nothing depends on `roof`.
- Q: Where do the new settings live? → A: In `alerts.events.<event>` like the imaging-app events,
  stored with the main alerts settings. A config saved before this spec loads with the defaults.
- Q: Does this spec add the roof's state or alerts to `/api/status`, MQTT state topics or the
  dashboard? → A: No: specs 008 and 009.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| II | Park not confirmed: stay open and raise a critical alert | FR-002, FR-004: "mount didn't park" can't be switched off and isn't held by any pause |
| III | A timed-out step raises a critical alert naming the step | FR-002, FR-004 |
| III | Every reason states the value and the limit, or the input that failed | FR-005, FR-009 |
| III | The alert path doesn't depend on the device that failed | FR-004: alerts never go through N.I.N.A. or SQMeter |
| III | Alerts report the verdict and can never change it or the motion decisions | FR-016: nothing in this spec feeds back into the sequence or the roof |
| I | Any motion fault stops the motor and locks out motion until a person clears it | FR-007 reports it; spec 002 decides it |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The critical roof alerts stay loud and can't be lost (Priority: P1)

The owner is asleep. The mount fails to park, or the roof fails to close in the rain. The alert
reaches every channel that is switched on, at the level the owner chose (never below "urgent"),
whatever the pause, grace or master switch say, and it is sent again until a channel takes it.

**Why this priority**: These are the alerts the constitution requires; spec 005's guarantees must
survive the move to configurable events.

**Independent Test**: The settings-to-alert logic, fed each of spec 005's critical decisions and
every combination of pause, master switch and stored level, always yields an alert of the right
event at "urgent" or "wake"; the close-sequence tests and the resend tests of spec 005 still pass.

**Acceptance Scenarios**:

1. **Given** alerts paused and the master switch off, **When** the mount doesn't park, **Then**
   "Mount didn't park" goes to every channel that is switched on, at the event's level.
2. **Given** the event's level set to "urgent", **When** it fires, **Then** it is sent as urgent;
   **Given** a stored level of 0, 1 or 2 (an edited file), **Then** it is sent as urgent.
3. **Given** a channel that fails, **When** the alert isn't taken, **Then** it is sent again after a
   minute, then at doubling gaps up to 30 minutes (spec 005, unchanged).
4. **Given** custom wording without `{reason}` or `{details}`, **When** it fires, **Then** the
   built-in message follows the custom one.
5. **Given** a reminder 30 minutes later, **Then** its title starts with "Still: ", with custom
   wording too.
6. **Given** the settings page, **When** the owner tries to set a critical event to off, quiet or
   normal, **Then** those choices aren't offered, and the device refuses them in a saved config.

---

### User Story 2 - Told when the roof faults or the supply sags (Priority: P1)

A move times out or jams, or the 12 V supply sags. The owner is told what happened, with the value
and the limit, at the level they chose.

**Why this priority**: A faulted roof needs a person before it can close again.

**Independent Test**: The alert engine, fed a fault that latches, clears and latches again, and a
supply reading that dips below its minimum for 29 s and then for 30 s, raises exactly the expected
alerts, respecting the grace, the cooldown and the level.

**Acceptance Scenarios**:

1. **Given** alerts on, **When** a move times out, **Then** "Roof fault" says which fault, with its
   value and limit.
2. **Given** the fault still latched, **Then** no further "Roof fault" alerts; **When** it is cleared
   and another latches, **Then** a new alert (after the cooldown).
3. **Given** the supply below its minimum for 29 s and then back, **Then** nothing; for 30 s,
   **Then** "12 V supply low" with the reading and the minimum.
4. **Given** a fault latched before a power cut, **When** the device starts again, **Then** "Roof
   fault" goes out once the start-up grace ends; **Given** a reset instead, **Then** it isn't
   repeated.

---

### User Story 3 - Told when the weather closes the roof (Priority: P2)

The owner wants to know that the roof started closing because of the weather and that it reached
closed.

**Why this priority**: Peace of mind; the failure cases are already critical alerts.

**Independent Test**: The alert engine, fed the automatic close's state over an episode, raises
"Unsafe: closing" once at the start and "Roof closed after unsafe weather" once at the end; a
flapping verdict raises at most one "Unsafe: closing" per cooldown.

**Acceptance Scenarios**:

1. **Given** the roof open, **When** the weather turns unsafe, **Then** "Unsafe: closing" with the
   weather's reasons.
2. **Given** the roof closes, **Then** "Roof closed after unsafe weather".
3. **Given** the verdict flipping every 10 s for 10 minutes with a 5-minute cooldown, **Then** at
   most two "Unsafe: closing".
4. **Given** the roof can't move by itself when the weather turns, **Then** no "Unsafe: closing" (the
   critical "Roof didn't close" says why).

---

### User Story 4 - Told when a weather source is lost (Priority: P2)

SQMeter stops answering or its MQTT messages stop. The owner learns which source and for how long.

**Why this priority**: A lost source makes the verdict unsafe and closes the roof; knowing why
saves a trip.

**Independent Test**: The engine, fed a source that is lost for 29 s and then for 30 s, alerts once,
with the source's own text.

**Acceptance Scenarios**:

1. **Given** SQMeter over HTTP switched on, **When** it has been unreachable for 30 s, **Then**
   "Weather source lost" names it and why.
2. **Given** no weather source switched on, **Then** nothing.
3. **Given** the source back and lost again, **Then** a new alert after the cooldown.

---

### User Story 5 - My own levels, sounds and words, and a test (Priority: P2)

The owner sets each roof event's level, sound and wording on Settings > Alerts, as for the
imaging-app events, and sends a test of each.

**Why this priority**: SQMeter's alert design, which the owner already uses.

**Independent Test**: The settings round trip (device and web schema), the event rows (Vitest) and
the test send of each event.

**Acceptance Scenarios**:

1. **Given** Settings > Alerts, **Then** a row for each of the seven roof events with level, sound
   (when Pushover is on), wording and Test.
2. **Given** the master switch off, **Then** the critical rows can still be edited and tested, with a
   note that they are sent even when alerts are off or paused.
3. **Given** a template with `{reason}`, **When** the test is sent, **Then** an example reason is
   filled in.
4. **Given** a config saved before this spec, **When** it loads, **Then** the roof events have their
   defaults.

### Edge Cases

- A fault latched by the automatic close: both "Roof didn't close" (critical) and "Roof fault" go
  out; they are different events.
- Several non-critical events in the same pass go out as one notification at the loudest level.
- The master switch turned on while a fault is latched: the fault isn't announced then (it wasn't a
  change while alerts were on).
- A critical alert while the alert task is busy: it waits in the queue like any alert; a full queue
  fails it and the resend takes over (spec 005).
- The custom wording of a critical event with a typo in a variable name: the name stays visible, and
  the built-in message is added if neither `{reason}` nor `{details}` is used.
- The cooldown setting is 0: the roof's condition events still wait a minute between two alerts of
  the same event, so a flapping verdict can't send one per flip.
- A long uptime: the engine's time base is seconds since start from a 64-bit clock (136 years
  before it wraps).
- A custom critical message that renders past Pushover's 1,024-character limit: the built-in
  message is sent instead, so the resend can't retry a refused message for ever.

## Requirements *(mandatory)*

### Functional Requirements

**Events**

- **FR-001**: The device MUST offer the seven roof events above, each with a stable name used in
  webhook and MQTT payloads and the alert settings: `mount_not_parked`, `roof_not_closed`,
  `move_fault`, `supply_low`, `unsafe_closing`, `roof_closed`, `safety_lost`. The placeholder `roof`
  event MUST be removed.

**Critical events**

- **FR-002**: Spec 005's critical decisions MUST map to the events: mount not parked to
  `mount_not_parked`; roof didn't close, roof can't close by itself and stopped by a person to
  `roof_not_closed`. The built-in title and message stay spec 005's.
- **FR-003**: A critical event's level MUST be "urgent" or "wake me" (default "wake me"), and its
  sound MUST NOT be silent or vibrate only. The device MUST refuse a settings change with a lower
  level or a silent sound for it; settings read back from storage with one MUST be raised (to urgent,
  the default sound) rather than refused, so the alert channels are never lost with them; and the
  device MUST send it as "urgent" with an audible sound whatever is stored. The web UI MUST offer
  only those two levels and no silent sounds.
- **FR-004**: Critical events MUST go to every channel that is switched on, regardless of the alert
  pause, the send mode, the start-up grace and the master switch; the 30-minute throttle and
  reminders, and the resend of an alert no channel took, MUST stay as spec 005 defines them. They
  MUST NOT go through N.I.N.A. or SQMeter.
- **FR-005**: A critical event MAY have custom wording. A custom message that contains neither
  `{reason}` nor `{details}` MUST be followed by the built-in message. A reminder's title MUST start
  with "Still: ".

**Other events**

- **FR-006**: The five other events MUST follow the usual alert rules: not sent while alerts are off,
  paused, or the event's level is off; not sent during the start-up grace, a condition still present
  when it ends being announced then; each at most once per `alerts.cooldownSeconds` (for the four
  condition events at least once a minute, even with a cooldown of 0), a change held back by the
  cooldown sent when it ends if the condition still holds; stacked with any other alert of the same
  pass.
- **FR-007**: `move_fault` MUST fire when a motion fault latches, with the fault's text (its value
  and limit, or the input that failed), and not again until the fault has been cleared.
- **FR-008**: `supply_low` MUST fire when the supply has read below `roof.minSupplyVolts`, or been
  unreadable, for 30 s without a break, with the reading and the minimum, and not again until it has
  been back at or above the minimum.
- **FR-009**: `unsafe_closing` MUST fire when the automatic close begins an attempt (waiting for the
  park, closing, or waiting to retry), with the weather verdict's reasons; not while the sequence
  can't move the roof or is stood down.
- **FR-010**: `roof_closed` MUST fire when the roof reaches closed while the automatic close is
  attempting a close, held through the start-up grace and the cooldown; a closed reading that returns
  while the sequence can't move or is stood down MUST NOT fire it.
- **FR-011**: `safety_lost` MUST fire when any weather source that is switched on has been stale,
  unreachable, unreadable or offline (the rain contact: unreadable) for 30 s without a break, naming
  each lost source and why; not when no source is switched on and not during the start-up wait for a
  first reply; not again until every source is back.
- **FR-012**: A `move_fault`, `supply_low`, `safety_lost` or `unsafe_closing` condition already
  announced before a reset MUST NOT be announced again after the reset while it lasts (a weather
  source still waiting for its first reply after the reset doesn't count as back). After a power cut
  it is announced once the start-up grace ends.

**Settings and UI**

- **FR-013**: Each roof event MUST have the usual event setting (level 0-4, sound, title up to 80
  characters, message up to 240) in `alerts.events.<name>`, stored with the main alerts settings, with
  defaults (the size check applies when settings are saved, not when stored ones are loaded):
  `mount_not_parked` 4, `roof_not_closed` 4, `move_fault` 3, `supply_low` 2,
  `unsafe_closing` 2, `roof_closed` 2, `safety_lost` 2. A config saved before this spec MUST load with
  them. The total size check of the stored alerts settings MUST still apply.
- **FR-014**: Settings > Alerts MUST show a row per roof event using the existing event row (level,
  sound, wording, Test), the critical ones editable and testable while alerts are off with a note
  that they are always sent; the settings-dependencies catalogue MUST list the five non-critical
  levels under "alerts on" (D-04) and not the critical ones.
- **FR-015**: Each roof event MUST have a test send with example values, and its template variables
  MUST be listed in the shared variable list: `{reason}` for every roof event; `{limit}` and
  `{details}` for the critical ones.

**Boundaries**

- **FR-016**: Nothing in this spec MAY change the weather verdict, the close sequence's decisions or
  any motion decision; the alert path only reads them.

**Docs**

- **FR-017**: The alerts user guide MUST describe each roof event, when it fires, its default level
  and its variables, and that the critical ones can't be switched off or paused; the configuration
  and REST references MUST list the new event settings and names.

### Key Entities

- **Roof event**: name, whether it is critical, default level, the variables it fills.
- **Roof alert conditions**: what the alert engine reads each pass: whether a fault is latched (and
  its text), whether the supply is low (and its text), whether a weather source is lost (and its
  text), whether the automatic close is attempting a close (and the weather's text), and how many
  times the roof has reached closed during an automatic close.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every critical decision of spec 005 and every combination of pause, send mode,
  master switch, grace and stored level, a native test shows the alert is produced at "urgent" or
  "wake" (100% of combinations).
- **SC-002**: Every spec 005 native test (close sequence, stand-down, text, properties, restart,
  alert resend) passes unchanged.
- **SC-003**: A randomised test of at least 5,000 runs of roof conditions changing over time never
  sends a non-critical roof event twice within the cooldown, never during the grace, and never
  misses a condition that held for its settle time plus the cooldown outside the grace with alerts
  on.
- **SC-004**: Each settle time (30 s) and the cooldown are tested one second before, at and after
  their boundary.
- **SC-005**: The full local check (firmware build, native tests, web tests, Playwright, quality gate,
  docs build, flash budget) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Two critical events, not four.** "Roof didn't close" covers spec 005's close failure, "can't
   close by itself" and "stopped by a person", with spec 005's own titles telling them apart. One
   setting covers every case in which the roof is open while unsafe and needs a person.
2. **Critical events can't go below "urgent"**, and the device sends a lower stored level as urgent.
   "Normal" would let Pushover's quiet hours silence a roof left open in the rain.
3. **Critical events keep custom wording, but never lose their reason**: the built-in message is
   added when the custom one doesn't use `{reason}` or `{details}`.
4. **Critical events ignore the pause, the send mode, the grace and the master switch**, as in spec
   005; only the channels switched on are used. With no channel set up, the serial log is still the
   only report.
5. **The other five follow the usual rules**, including the pause and "only while an imaging app is
   connected". The owner who pauses alerts while away hears only the critical ones.
6. **Default levels**: critical "wake me"; roof fault "urgent"; the rest "normal".
7. **A condition present at start-up is announced after the grace** ("unsafe: closing" included,
   when the restored sequence is attempting), unless already announced
   before a reset (RTC memory: survives a reset, not a power cut, and not reliably a firmware
   update). A reset loop doesn't repeat it; a power cut does once.
8. **30 s settle for the supply and a lost source**, so a motor start's dip and a single failed
   request don't alert. The roof's own motion fault for the supply is unchanged (spec 002).
9. **No weather source switched on is not "lost"**: it is a setting the owner made.
9a. **The roof's condition events wait at least a minute between alerts** of the same event, even
    with the cooldown set to 0 (which the imaging-app events honour as SQMeter does).
10. **"Unsafe: closing" covers the attempt phases only**, at most once per cooldown; nothing extra
    when the weather clears before the close (logged only), and no "weather safe again" event.
11. **"Roof closed after unsafe weather" counts only a close after an attempt**, at most once per
    cooldown, so a marginal closed magnet can't send one per flicker.
11a. **Critical alerts can't be silent**: "none" and "vibrate" aren't offered and are refused; a
    stored one is sent with the default sound.
11b. **Stored settings with a critical level below urgent load with urgent**, rather than being
    refused (which would lose every alert channel with them); the stored-size check applies on save.
12. **A fault latched by the automatic close raises both** the critical "Roof didn't close" and
    "Roof fault".
13. **The `roof` event name is dropped** without an alias: no release ever carried it.
14. **The settings rows reuse the existing event row and card** (no new components or styles,
    constitution VII); the owner's review of the page is a bench check, as the dashboard and Roof
    settings tab are spec 009's.

## Out of scope

- The roof and sequence in `/api/status`, MQTT state topics and Home Assistant (008), the dashboard
  and the Roof settings tab (009), the demo's simulated roof and its injected faults (011), the
  Alpaca Dome and its client watch (006).
