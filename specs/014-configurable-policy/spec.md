# Feature Specification: Configurable Policies

**Feature Branch**: `spec/014-configurable-policy`

**Created**: 2026-11-11

**Status**: Draft

## Context

RoofRoof is not the thing that runs the night. The imaging app's sequence (N.I.N.A.) opens the
roof over the ASCOM Alpaca Dome at dusk and closes it the same way at dawn or when the session
ends. RoofRoof sits behind it as a failsafe: it closes the roof onto a parked mount when the
weather turns unsafe (spec 005) or, if asked, when the sun comes up (spec 013), and it refuses
anything that would break the safety rules whoever asks.

Specs 002 to 013 each had open choices. Each spec took the safest option and wrote it down under
"Assumptions decided for the owner". Many of those choices are rules the constitution demands and
must never change. Many others are only a policy or a tuning value (how long a stand-down lasts,
how often a park request is repeated, how often a reminder goes out) that the owner may
reasonably want different at his site. Those are fixed in the code today.

This spec does three things:

1. **An audit** of every owner assumption in specs 002 to 013 and of the hard-coded timings in
   `lib/`, sorting each into one of three classes:
   - **(a) safety invariant**: required by constitution I to III or VIII. It stays fixed.
   - **(b) policy or tuning**: becomes a setting, with today's value as its default and a range
     that cannot break any (a) item; or stays fixed, with the reason, where no range would be
     safe or useful.
   - **(c) hardware or measurement**: already a setting, or a fact of the board and the loop.
2. **Settings** for the (b) items that become settings, in Settings → Roof under an "Advanced"
   fold, so the page stays as quiet as it is today. With every new setting at its default the
   device behaves exactly as it does on `main`.
3. **The N.I.N.A. opening path made plain**: Settings and the docs say that N.I.N.A. opens and
   closes the roof over the Alpaca Dome, that Alpaca is off until turned on, and how to turn it
   on. The first-setup wizard (spec 015, built in parallel) links to one doc anchor for this.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Which stand-down setting: only a person's stop, or every "safe for 10 minutes" in the close
  sequence? → A: **One setting, "Safe spell to re-arm" (`autoClose.rearmMinutes`), for every use
  of the 10-minute safe spell** in specs 005 and 013: a person's stop, a halted close and a
  can't-move stand-down re-arm after it; a safe spell shorter than it doesn't restart an attempt;
  the wall-clock caps add it; daylight takes a weather episode over after it. Splitting them
  would let a combination of values hold a stand-down longer than the episode it belongs to,
  which no spec has analysed. Range 5 to 60 minutes.
- Q: Should Alpaca be on by default now that N.I.N.A. is the normal way the roof opens? → A:
  **No, it stays off** (see "Alpaca's default" below). Settings → Roof says so with a link to turn
  it on, and the docs give the steps at one anchor.
- Q: May a remote client's Stop call off a close in unsafe weather? → A: **A setting, off by
  default.** With it on, an Alpaca or MQTT Stop during a close in unsafe weather (or a daylight
  close) is accepted and counts exactly as a person's Stop: the automatic close stands down and
  raises its critical "Automatic close stopped" alert, with the reminders. It can never leave the
  roof open in unsafe weather without that alert. With it off (the default) nothing changes.
- Q: Is "no open while the setup hotspot is on" a setting? → A: **No, it stays fixed (a).** The
  hotspot has no password and Alpaca has no login, so with the rule off anyone in range could
  join the hotspot and open the roof over Alpaca. The hotspot is not the trusted LAN of
  constitution VIII.
- Q: Is "a committed restart refuses a close" a setting? → A: **No, it stays fixed (a).** A close
  allowed in the milliseconds before the reset would be cut off part-way; the position is then
  unknown and the roof can't close by itself, which leaves it open in the rain (constitution I and
  III). Refusing it costs one retry.
- Q: Halt or retry when a waiting close never starts? → A: **A setting, default halt** (spec 005's
  choice). Retry calls the waiting close off, raises "Roof didn't close" with its own cause and
  backs off as for a refused close; the next try still needs the park confirmed again.
- Q: Which alert intervals become settings? → A: **One, the critical reminder interval**
  (`autoClose.remindMinutes`, 10 to 120 minutes): how often a critical event can alert again and
  how often it is repeated while the roof stays open, and the longest gap between resends of a
  critical alert no channel took (spec 005 tied the two together). The first resend after a minute,
  the two-minute pending limit, the 30 s settle and the one-minute floor of spec 007's roof events
  stay fixed.
- Q: Does a confirmation dialog for Open and Close become an option? → A: **Yes, off by default**
  (spec 009's choice). With it on, Open and Close ask once inline before they are sent. Stop never
  asks (constitution VII).
- Q: Which MQTT cadence becomes a setting? → A: **The at-rest change gap** (1 to 60 s, default 1).
  The publish interval is already a setting. The busy cadence (at most two publishes 5 s apart
  while the roof moves or a close waits) stays fixed: it protects the main loop the roof's checks
  depend on.
- Q: Does the daylight band become a setting? → A: **Yes**, `daylight.bandDeg`, 0.1° to 2.0°,
  default 0.5°.

## Alpaca's default

Alpaca stays **off** by default for a new device and after an upgrade. Reasons:

- Constitution VIII: Alpaca has no login, and it can move the roof. A device should only start
  serving it once its owner knows it is on a trusted LAN. Spec 006 recorded the same decision.
- A new device hasn't been commissioned: travel, speed and the Hall thresholds are still the
  conservative defaults. An imaging app that finds it by discovery before the owner has calibrated
  it could start moves the owner didn't plan.
- The compatibility rule forbids a migration that turns a motion feature on, so an upgrade must
  keep it off anyway. Having a new device and an upgraded one behave the same is simpler to
  explain.
- Off costs one switch. This spec makes the switch easy to find: Settings → Roof shows a note when
  Alpaca is off ("Imaging apps can't open or close the roof") with a link to the ASCOM Alpaca card,
  and the docs anchor [`alpaca.md#nina-opens-and-closes-the-roof`](../../docs/user-guide/alpaca.md)
  gives the steps; spec 015's setup wizard links there.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | The roof never moves by itself from an unknown position; any motion fault latches until a person clears it | FR-003: no setting touches the motion logic, the fault latch or the position rules |
| I | OTA, restarts and settings that change motion are refused while moving | FR-004: every new setting is saved through the same refusal |
| II | Close only onto a confirmed park, checked the same way for every source | FR-005: the park hold can only be raised from 4 s and the close still passes spec 003's park gate |
| III | Unsafe ends closed; a timed-out step stops and raises a critical alert naming it | FR-006 to FR-009: every timing has a range; a stand-down always raises its critical alert; reminders can be made rarer but never off |
| III | Unknown weather is never safe | FR-003: no setting touches the verdict |
| III | The roof never opens by itself | FR-003: no setting adds an open |
| III | Alerts, MQTT and the UI never change the verdict or the motion decisions | FR-010: a remote stop counts as a person's only with the owner's setting, and raises the alert |
| VII | Stop is always reachable and never disabled | FR-012: the confirmation option never applies to Stop |
| VIII | Trusted LAN; Alpaca open by design; no internet exposure | "Alpaca's default"; FR-011: the hotspot open rule stays fixed |

The tests that prove each rule are listed in [plan.md](plan.md#constitution-check).

## Audit *(mandatory)*

Each owner assumption of specs 002 to 013, and each timing hard-coded in `lib/`, in one line.
"Setting" names the new setting; "fixed" gives the reason it stays. Spec numbers refer to the
numbered items in each spec's "Assumptions decided for the owner".

### Spec 002 (roof motion logic)

| Item | Class | Decision and reason |
|---|---|---|
| Stop keeps the estimate, abort drops it | a | I: position is known only at a magnet or from a fault-free move |
| A move from "stopped" uses the normal profile and the remaining distance's timeout | a | I: every move has a hard timeout |
| Supply low at rest refuses, while moving faults | a | I: a fault stops the motor and latches |
| A Hall rail reading latches a fault at rest | a | I: a disconnected sensor needs a person |
| Reversing goes through stop and the dead time | a | I: RPWM and LPWM never together |
| Speed taken as proportional to duty | c | A model of the motor; the timeout margin is the setting that covers it |
| Dead time, Hall confirmation, hysteresis, rails, 250 ms confirm, 100 ms step gap, rail 3 of 8, unreadable limits | c | Facts of the ADC and the control loop; a shorter dead time or step gap would weaken I |
| Commands carry no time | a | I: no client can shorten the dead time |
| Drive budget per direction | a | I: no move gets endless fresh timeouts |
| Abort with no move keeps the position | a | I: nothing moved |
| Open direction, leave time, supply minimum, thresholds | c | Already settings |
| Fault wording | a | III: the value and the limit are always given |

### Spec 003 (roof hardware)

| Item | Class | Decision and reason |
|---|---|---|
| 1. Park checked before and throughout every close; open refused unless safe | a | II, III |
| 2. An open stops when the weather stops being safe | a | III |
| 3. Park lost during a close is a latched fault | a | II |
| 4. Park switch and rain contact polarity fixed | a | II: an inverted input would read a broken wire as parked |
| 5. Three equal readings, one second of chatter is unreadable | c | A debounce of the input hardware |
| 6. REST commands are a person's | a | I, VIII: authenticated |
| 7. Stop needs authentication | a | VIII |
| 8. Current-sense scale | c | Already a setting |
| 9. `roof.*` and `park.*` saves refused while moving | a | I |
| 10, 11. Watchdog at the 100 ms step gap, 20 ms control rate | c | Facts of the loop; I depends on them |
| 12. A damaged fault record latches | a | I |
| 13. PWM 20 kHz | c | The driver's rating |
| 14, 15. No motor without the task and watchdog; a watchdog trip survives a restart | a | I |
| 16, 16a, 16c. Known limits, AsyncTCP priority, current sense under PWM | c | Platform facts; the broken-wire floor is a setting |
| 16b. `X-RoofRoof: 1` on roof commands | a | VIII |
| 16d. A close wins over an upload | a | III |
| 17. Demo roof | — | Replaced by spec 011 |

### Spec 004 (safety inputs)

| Item | Class | Decision and reason |
|---|---|---|
| 2. Park switches none, one or two | c | Already a setting |
| 3. SQMeter HTTP stale time | c | Already a setting (10 to 600 s) |
| 3. SQMeter MQTT stale 150 s, N.I.N.A. stale 10 s | b, fixed | Tied to SQMeter's fixed republish and to the poll period; a longer value only delays acting on doubt (III) |
| 4. Poll periods and request timeouts | c | Network budget (VI) |
| 5, 6. Unknown only in the start-up window; no source is unsafe | a | III |
| 7. `http://` only | c | No TLS of its own (VIII) |
| 8, 9, 12, 13, 14. `safe:true` with reasons is unsafe; MQTT needs `online`; rain contact only adds unsafe; one failed poll waits for the stale time; rain stays a reason | a | III |
| 10, 11. Slewing or disconnected is not parked; "already parked" still needs confirming | a | II |

### Spec 005 (close on unsafe)

| Item | Class | Decision and reason |
|---|---|---|
| 1. Unknown counts as unsafe | a | III |
| 2. No open override | a | III |
| 3. A person's stop stands the sequence down, with one critical alert | a | VII and III: Stop always works and is never silent |
| 3, 11a, 11d, 11i. The 10-minute safe spell that re-arms a stand-down or halt, spaces attempts and caps them | b | **Setting** `autoClose.rearmMinutes`, 5 to 60 min. Below 5 a flapping verdict could restart attempts without the park alert; above 60 a stop would hold back the next spell too long |
| 4. Park time limit | c | Already a setting |
| 5. Park requests 30 s apart | b | **Setting** `autoClose.parkRequestSeconds`, 10 to 120 s. Below 10 s would flood N.I.N.A.; the park limit still runs |
| 5, 9. Five minutes after a failed attempt or a refused close | b | **Setting** `autoClose.retryMinutes`, 1 to 30 min. Each retry still needs the park confirmed |
| 6. The park must hold 4 s | b | **Setting** `autoClose.parkHoldSeconds`, 4 to 30 s. Never below 4 s: N.I.N.A. is polled every 2 s, and a shorter hold could be met by one reading (II); the gate checks it again |
| 7. A commanded close runs to the end | a | III |
| 8. Unknown position or fault: no request, no motion, alert after 2 s | a | I; the 2 s is the Hall settling (c, fixed) |
| 9. Close guard: the move's timeout plus 30 s | b | **Setting** `autoClose.closeGuardSeconds`, 10 to 120 s. The motion logic's own timeout still faults first (I) |
| 9. A waiting close must start within 30 s | b | **Setting** `autoClose.closeStartSeconds`, 25 to 120 s. Not below 25 s: a Wi-Fi change holds a close up to 15 s (spec 010), and that must not count as a failure |
| 10, 11f. Critical alerts at most every 30 minutes per event and cause, reminders every 30 minutes | b | **Setting** `autoClose.remindMinutes`, 10 to 120 min. Never off: a stand-down's alert always goes out and is always repeated (III) |
| 10. Critical alerts ignore the pause, the grace and the master switch | a | III: the alert path must not depend on what failed |
| 11b, 11c, 11k, 11l. Stop while unable to move stands down; parked held by back-off is not a failure; "controller didn't start"; an open stops when unsafe | a | III, I |
| 11e, 11j, 12. RTC record, restart allowance 10 s, sequence on the main loop | c | Platform facts; the allowance keeps a restart loop reaching its limit (III) |
| 11f. Three gate refusals count as a refused close | b, fixed | A count, not a time: more would hide a disagreeing park longer (II) |
| 11g. A waiting close that never starts halts | b | **Setting** `autoClose.notStarted`: `halt` (default) or `retry`. Retry calls the waiting close off and backs off; nothing has moved |
| 11h. Undelivered critical alert: again after 1 min, doubling up to 30 min; pending over 2 min failed | b | The 30-minute cap follows `autoClose.remindMinutes` (spec 005 tied them); the first minute and the 2-minute pending limit stay fixed so a lost alert is retried soon |

### Spec 006 (Alpaca Dome)

| Item | Class | Decision and reason |
|---|---|---|
| 1. A client is not a person: no move from unknown, no clearing a fault | a | I, VIII |
| 2. A client can't stop a close in unsafe weather | b | **Setting** `autoClose.remoteStop`, off by default. On: the stop counts as a person's, so the stand-down's critical alert and reminders always follow (III) |
| 2a. AbortSlew is Stop, not Abort | a | I, III: abort would make the position unknown and stop the roof closing itself |
| 2b. A client can't reverse a close | a | III |
| 3. Open and close need a connected client | a | IV |
| 4. Nothing moves on a client disconnect | a | III: never opens by itself |
| 5, 6, 7, 9a, 10. Shutter state mapping, waiting close reads closing, InvalidOperation, names, interface version | a | IV |
| 8. Alpaca off by default | a | VIII; see "Alpaca's default" |
| 9, 11. Demo and simulator | — | Test tooling |

### Spec 007 (roof alerts)

| Item | Class | Decision and reason |
|---|---|---|
| 1. Two critical events | — | Event design |
| 2, 11a, 11b. Critical events never below urgent, never silent | a | III: the alert must reach someone |
| 3, 11c. Custom wording keeps the reason; over-long wording falls back | a | III |
| 4. Critical events ignore pause, send mode, grace, master switch | a | III |
| 5, 6. Other events follow the usual rules; default levels | c | Already settings |
| 7. Announce a condition after the grace | b, fixed | A one-off at start-up; nothing to tune |
| 8. 30 s settle for the supply and a lost source | b, fixed | Measured against a motor start's dip and one failed request; the motion logic faults on a real low supply (I) |
| 9a. Roof condition events at least a minute apart | b, fixed | The cooldown is already a setting; the floor stops a flickering magnet sending a storm |
| 9, 10, 11, 12, 13, 14. Event scope and settings rows | — | Event design |

### Spec 008 (data interfaces)

| Item | Class | Decision and reason |
|---|---|---|
| 1. MQTT has the client's rules | a | VIII, III; shares `autoClose.remoteStop` with Alpaca |
| 2. MQTT roof commands off by default | c | Already a setting; a migration keeps it off |
| 3, 8. Retained commands never acted on; nothing queued | a | VIII, III: nothing stale can run |
| 4. No clear-fault over MQTT | a | I |
| 5, 6, 7, 9, 12. Document shape, cover, sensors, what is shown | — | Interface design |
| 10. Publish interval | c | Already a setting |
| 10. At rest, a change published at most once a second | b | **Setting** `mqtt.changeMinSeconds`, 1 to 60 s |
| 10. While busy, at most two publishes 5 s apart | b, fixed | Keeps the main loop free for the roof's checks (III: a stale hand-over stops a close) |

### Spec 009 (dashboard and settings)

| Item | Class | Decision and reason |
|---|---|---|
| 1. No Open or Close without fresh live data | b, fixed | A button acting on stale state is the greater risk; nothing to tune |
| 2. The page only predicts refusals | a | III: the UI can't change a decision |
| 3. Busy, restarting and hotspot in the buttons | — | Mirrors the device's rules |
| 4. Stop bar on every page | a | VII |
| 5. No confirmation dialogs | b | **Setting** `ui.confirmOpenClose`, off by default; Stop never asks (VII) |
| 6–11. Warnings, Status card, editing while moving, Hall scale, API version, review | — | UI design |

### Spec 010 (network under motion)

| Item | Class | Decision and reason |
|---|---|---|
| 1. Uploads, restarts and Wi-Fi changes refused while the sequence works | a | III |
| 2. RTC record, not NVS | c | Flash wear and writes during moves (I) |
| 3. No open while the setup hotspot is on | a | VIII: an open hotspot isn't a trusted LAN, and Alpaca has no login |
| 4. A committed restart refuses close | a | I, III: a close cut off by the reset leaves the position unknown, and the roof open |
| 5. Wi-Fi loss adds nothing | a | I |
| 6. Restart timer retries after 100 ms | c | Shares the watchdog's timer task |
| Network hold of a close at most 15 s | c | Must stay under the close-start floor of 25 s (FR-007) |

### Spec 011 (demo and bench mode)

| Item | Class | Decision and reason |
|---|---|---|
| Bench mode a separate build; refused over OTA; refuses with 12 V present | a | I |
| Bench start half-way, no fault injection, labels, demo defaults | — | Test tooling |

### Spec 012 (commissioning)

Procedures, not device behaviour. The guide and checklist name the new settings where they matter
(stage 5 rows, stage 7 dry run).

### Spec 013 (daylight failsafe)

| Item | Class | Decision and reason |
|---|---|---|
| 1. Default mode alert | c | Already a setting |
| 2. Threshold and range | c | Already a setting |
| 2. The 0.5° band | b | **Setting** `daylight.bandDeg`, 0.1° to 2.0°: never 0 (the state would flicker) |
| 3, 14. No action and an alert after 10 minutes without the site or the clock | b, fixed | Lets NTP set the clock after a restart; nothing to tune |
| 4, 5, 6. Refuse opening with "alert and close"; unsafe weather re-arms a daylight stand-down after 30 s; hand-over | a | III: unsafe weather always ends closed. The 10 minutes of item 6 follow `autoClose.rearmMinutes` |
| 7–13, 15. Levels, wording, location names, status | c, — | Settings already, or event design |

### Hard-coded timings in `lib/` not named above

| Constant | Class | Decision and reason |
|---|---|---|
| `SAFETY_HANDOVER_STALE_MS` 5 s (gates) | a | II: the park result must be fresh when the close starts |
| `WEATHER_REARM_CONFIRM_MS` 30 s, `MAX_COUNT_STEP_MS` 1 s | b, fixed | One stale poll must not send a park request against a person's stop; ties spec 013's hand-over together |
| `UNKNOWN_CONFIRM_MS` 2 s | c | The Hall reading settling after a restart |
| `RESTART_ALLOWANCE_MS` 10 s | c | The restart's own time |
| `MAX_PARK_REFUSALS` 3 | b, fixed | See spec 005 item 11f |
| MQTT write budgets | c | Main-loop budget (VI) |
| `FORCED_ABOVE_DEG` 10° | — | The demo's "Sun up" |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Nothing changes until the owner changes it (Priority: P1)

An owner upgrades. Every new setting loads at today's value, and the roof, the close sequence,
the alerts, Alpaca and MQTT behave step for step as before.

**Why this priority**: The controller is a failsafe; an upgrade must never change what it does.

**Independent Test**: The native tests pin each default to today's constant, and a differential
run feeds the same random inputs to this branch's close sequence (with the default policy) and
to `origin/main`'s, and compares every decision.

**Acceptance Scenarios**:

1. **Given** a config saved by the previous release, **When** it loads, **Then** every new
   setting has its default and nothing is turned on.
2. **Given** default settings, **When** the close sequence runs any input sequence, **Then** its
   actions, events, causes, alerts and limits equal `main`'s.

---

### User Story 2 - The owner tunes the automatic close (Priority: P1)

An owner whose mount takes a long time to report parked, or whose N.I.N.A. dislikes repeated
park requests, sets the request spacing, the hold, the back-off and the guard times in Settings →
Roof → Automatic close → Advanced. An owner who wants a person's Stop to hold for an hour sets
the re-arm spell to 60 minutes.

**Why this priority**: The reason for this spec.

**Independent Test**: Native tests at each range's limits show the sequence uses the value, and
that values outside the range are refused on save and clamped by the logic.

**Acceptance Scenarios**:

1. **Given** a re-arm spell of 5 minutes, **When** a person stops the automatic close and the
   weather is then safe for 5 minutes, **Then** the stand-down ends (and not before).
2. **Given** the not-started policy "retry", **When** a waiting close hasn't started within the
   close-start time, **Then** the close is called off, "Roof didn't close" goes out naming the
   close that didn't start, and the next try comes after the back-off with the park confirmed
   again.
3. **Given** a reminder interval of 60 minutes, **When** a stand-down lasts 2 hours, **Then** its
   critical alert goes out at once and is repeated after 60 and 120 minutes.
4. **Given** any value in its range, **When** the roof is unsafe and the park is not confirmed,
   **Then** the roof never closes (constitution II).

---

### User Story 3 - N.I.N.A.'s Stop works in bad weather, if the owner wants it (Priority: P2)

An owner who is often at the roof with N.I.N.A. open turns on "Remote stop calls off the close".
N.I.N.A.'s Stop during an automatic close then stops the roof and stands the close down, with
the same critical alert as a person's Stop.

**Independent Test**: Native tests of the command check and the sequence with the setting on and
off, for Alpaca and MQTT origins.

**Acceptance Scenarios**:

1. **Given** the setting off, **When** an Alpaca client sends AbortSlew during a close in unsafe
   weather, **Then** it is refused as today.
2. **Given** the setting on, **When** it does, **Then** the roof stops, the close sequence stands
   down and raises "Automatic close stopped" with its reminders.

---

### User Story 4 - Setting up N.I.N.A. is plain (Priority: P2)

A new owner wonders how N.I.N.A. opens the roof. Settings → Roof says Alpaca is off and imaging
apps can't open or close the roof, with a link to the ASCOM Alpaca card; the docs give the steps
at one anchor that the setup wizard links to.

**Acceptance Scenarios**:

1. **Given** Alpaca off, **When** the owner opens Settings → Roof, **Then** a note says imaging
   apps can't open or close the roof, with a button to the Alpaca card.
2. **Given** Alpaca on, **When** the owner opens Settings → Roof, **Then** no note shows.

---

### User Story 5 - Open and Close ask first, if the owner wants (Priority: P3)

An owner who has pressed Open by mistake on a phone turns on "Ask before Open and Close". The
Roof card's Open and Close then ask once inline; Stop never asks.

---

### Edge Cases

- A setting changed while an attempt runs applies from the next step; a value already exceeded
  (a shorter guard than the close has run) acts on that step, as a timeout would.
- A stored value outside its range (edited by hand, or from a later release) is refused on save
  and loaded at the nearest limit by the logic, which clamps every value it is given.
- The remote stop setting with Alpaca and MQTT roof commands both off does nothing; Settings says
  so.
- A remote stop when no close runs in unsafe weather is accepted as today and never counts as a
  person's.
- The confirmation option on a phone: the inline question uses the existing inline confirm
  pattern and fits at phone width.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The device MUST offer the settings below, each saved with the config, validated on
  save (refused outside its range with a message naming the range) and shown in Settings → Roof:

  | Setting | Default | Range |
  |---|---|---|
  | `autoClose.rearmMinutes` | 10 | 5–60 |
  | `autoClose.parkRequestSeconds` | 30 | 10–120 |
  | `autoClose.parkHoldSeconds` | 4 | 4–30 |
  | `autoClose.retryMinutes` | 5 | 1–30 |
  | `autoClose.closeStartSeconds` | 30 | 25–120 |
  | `autoClose.closeGuardSeconds` | 30 | 10–120 |
  | `autoClose.notStarted` | `halt` | `halt`, `retry` |
  | `autoClose.remindMinutes` | 30 | 10–120 |
  | `autoClose.remoteStop` | off | on, off |
  | `mqtt.changeMinSeconds` | 1 | 1–60 |
  | `daylight.bandDeg` | 0.5 | 0.1–2.0 |
  | `ui.confirmOpenClose` | off | on, off |

- **FR-002**: A config without these settings MUST load with the defaults, and the defaults MUST
  reproduce today's behaviour exactly.
- **FR-003**: No setting MAY change the motion logic, the fault latch, the position rules, the
  weather verdict, the park rule, the gates or anything that opens the roof.
- **FR-004**: Saving these settings follows the existing save rules (refused while the roof moves).
- **FR-005**: The close sequence MUST still close only after the park has held for the hold time
  and passes spec 003's park gate.
- **FR-006**: The close sequence's logic MUST clamp every policy value to its range, whatever it
  is given.
- **FR-007**: The close-start floor MUST stay above the longest network hold of a close (15 s).
- **FR-008**: With `notStarted` `retry`, a waiting close that hasn't started in time MUST be
  called off (a stop) and counted as a failed close with its own cause and wording, then back off.
- **FR-009**: Critical alerts MUST still go out at once for a new event and cause, and be
  repeated while the roof stays open; the interval only spaces them.
- **FR-010**: With `remoteStop` on, a remote client's stop that would be refused today MUST be
  accepted (a Stop; an abort stays refused) and counted as a person's stop; in every other case a remote stop behaves as today.
- **FR-011**: The hotspot open refusal and the committed-restart close refusal MUST stay fixed.
- **FR-012**: With `confirmOpenClose` on, Open and Close MUST ask once inline; Stop MUST never ask.
- **FR-013**: Alert texts that name the back-off or the re-arm time MUST use the set values.
- **FR-014**: Settings → Roof MUST show a note when Alpaca is off, with a button to the ASCOM
  Alpaca card. The docs MUST explain the N.I.N.A. path at one anchor.
- **FR-015**: The demo MUST apply the settings as the device does.
- **FR-016**: The settings-deps catalogue MUST list the new settings: the remote stop depends on
  Alpaca or MQTT roof commands; the daylight band on daylight being on.

### Key Entities

- **Close policy**: the timings and choices of the automatic close, taken from the config, with
  defaults equal to the constants of specs 005 and 013.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 and DS-03 (a new "Automatic close" settings card from the existing card and
  controls; one new component, an "Advanced" fold, because nothing in the design system folds
  rarely used rows away; it uses the existing tokens), DS-04 (every explanation behind "?"),
  DS-20 to DS-25 (labels, hints and notes within their limits). The confirmation uses the existing
  inline confirm pattern. No new button or card styles (constitution VII).
- **Mockup**: not reviewed; the owner reviews it in the demo build (a pending check in the PR).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every existing native and web test passes unchanged.
- **SC-002**: A differential run of at least 20,000 random input sequences shows this branch's
  close sequence, with default settings, deciding exactly as `origin/main`'s at every step.
- **SC-003**: Every new range is tested at its minimum and maximum (accepted) and just outside
  (refused), on the device and in the web UI, from the shared range fixture.
- **SC-004**: A randomised run with every policy value drawn from its range never closes without
  the park confirmed for the hold time, never opens, and raises a critical alert for every
  stand-down.
- **SC-005**: The full local check (firmware build, native tests, web tests, Playwright, quality
  gate, docs build, flash and UI budgets) passes with no new warnings.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **One re-arm setting for every use of spec 005's 10-minute safe spell** (stand-downs, halts,
   the pause window, the caps, spec 013's hand-over), 5 to 60 minutes.
2. **Alpaca stays off by default** for new devices and upgrades (constitution VIII, spec 006);
   Settings → Roof and the docs make turning it on easy.
3. **The remote stop is a setting, off by default**; on, it counts as a person's stop, so the
   stand-down's critical alert and reminders always follow, and the alert says an imaging app or
   MQTT stopped it. This relaxes spec 006's rule that nothing unauthenticated may call the close
   off in bad weather, only by the owner's choice and only through the same path as a person's
   Stop; the owner should confirm this reading when reviewing the PR. After a restart the
   reminder of such a stand-down says "a person" (the record doesn't keep who stopped it).
4. **The hotspot open refusal and the committed-restart close refusal stay fixed** (a).
5. **Halt stays the default** for a close that never starts; retry is the option.
6. **One reminder interval** for reminders, the per-event throttle and the resend cap, 10 to 120
   minutes; never off. The throttle and the resend cap never exceed spec 005's 30 minutes, so a
   long interval only spaces the reminders.
6a. **The park hold can only be raised** (4 to 30 s): N.I.N.A. is polled every 2 s, so a shorter
   hold could rest on one reading.
7. **The busy MQTT cadence, the 30 s settle, the one-minute roof event floor, the three gate
   refusals, the stale times of SQMeter MQTT and N.I.N.A., and spec 013's 30 s weather count stay
   fixed**, each for the reason in the audit.
8. **All new settings live in Settings → Roof**, the first four of the Automatic close card in
   view and the rest under "Advanced", plus the confirmation option on the Motion card, the band
   in the Daylight card's fold and the MQTT change gap in the Automatic close fold (the first
   setup wizard, spec 015, links to Settings for these).
9. **Settings changed during an attempt apply from the next step** (not from the next attempt,
   as the park time limit does): every value is in a safe range, and a stand-down shortened while
   it runs ends as if it had been set so from the start.
10. **The policy settings aren't refused while the roof is stopped part-way** (only roof motion
    settings make the position unknown); every save is refused while it moves (spec 010).
11. **The UI review is pending**: the owner reviews the Automatic close card in the demo build.

## Out of scope

- Changing any (a) item.
- MQTT or Home Assistant entities for the new settings.
- The first-setup wizard (spec 015).
