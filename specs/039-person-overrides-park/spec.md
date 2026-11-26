# Feature Specification: A Person May Open Past the Park

**Feature Branch**: `spec/039-person-overrides-park`

**Created**: 2026-11-26

**Status**: Draft

## Context

Spec 027 made the park gate Open as well as Close and let a person confirm past three refusals:
daylight and SQMeter's own "unsafe" for an Open, the park for one Close. An Open with the mount not
confirmed parked could only wait for N.I.N.A. to report the park. In the field the mount can be
parked, plainly so on the observatory camera, while N.I.N.A. doesn't say so (it lost the mount's
connection, or was restarted). The person at the web UI could close past the park but not open.

The owner decided (constitution 10.0.0): **the owner is the source of truth.** A person, at the
authenticated web UI, who can look at the camera may get past a safety check, with a clear warning
and a deliberate confirmation. **Automations** (an Alpaca client such as N.I.N.A., MQTT and Home
Assistant, the controller's own automatic moves) must never get past one.

Finding F31 is folded in: an Alpaca or MQTT Open refused for the weather must say the weather
source's own reasons (SQMeter's "Hold: Owner away (set from Home Assistant)"), not only "unsafe". The
firmware already did (spec 030); the bare "unsafe" seen was a truncated log line in the test
harness. An end-to-end test now guards it.

## User Scenarios & Testing

### User Story 1 - Open past the park after looking at the camera (P1)

1. **Given** the roof closed, the weather safe and the mount not confirmed parked, **When** a
   person presses Open, **Then** the Open is sent at once (the device asks N.I.N.A. to park when it
   can and waits) and the dialog lists the park with a red warning that the roof passes over the
   telescope and to check the camera, with **Wait for the park**, **Open anyway** and **Cancel**
   (which stops the wait).
2. **Given** that dialog, **When** the person presses **Open anyway**, **Then** the device opens
   without the park check (REST body `{"override": ["mountNotParked"]}`), logs it and sends the
   information alert "Park check overridden".
3. **Given** an open past the park under way, **When** the park result stays "not parked" or a
   park switch opens, **Then** the open carries on (the park interlock doesn't stop it).
4. **Given** the same open, **When** the weather turns unsafe (not overridden), daylight wants the
   roof closed (not overridden), a fault latches or anyone presses Stop, **Then** it stops as any
   open does.
5. **Given** an open past the park has ended (open, stopped, faulted, reversed), **When** the next
   Open comes, **Then** the park is checked again.
6. **Given** daylight or SQMeter's own unsafe as well as the park, **When** every reason can be
   overridden, **Then** **Open anyway** names them all; if any can't be (rain, a source in doubt,
   a fault, the supply), no override is offered.

### User Story 2 - No automation gets past a safety check (P1)

1. **Given** any of the checks in the table below, **When** an Alpaca client, MQTT or Home
   Assistant, or the controller's own move sends or carries an override, **Then** it is not
   honoured: the command is checked as if none were sent.

### User Story 3 - A weather refusal says why (P2, F31)

1. **Given** SQMeter reports unsafe with its reasons (for example a hold set from Home Assistant),
   over HTTP or MQTT, **When** an imaging app or MQTT asks to open, **Then** the refusal text is
   "Weather not safe: unsafe: SQMeter HTTP says unsafe: Hold: Owner away (set from Home
   Assistant)", and an imaging app's refused stop of a close says the same reasons.

## Requirements

- **FR-001**: A person's Open may carry `mountNotParked` (`OverridePark`); it sets the park gate
  aside for that one Open (start and re-check). Weather and daylight keep their own overrides; a
  suspect park switch (`parkSwitchCheck`) is not set aside (follow-up F-4).
- **FR-002**: `roof.openOverridable` includes `mountNotParked` while the park doesn't hold.
- **FR-003**: The override ends when the roof stops opening, by any path; `roof.override` shows
  `mountNotParked` while it lasts.
- **FR-004**: The park interlock doesn't stop an open with the park overridden; it still watches a
  calibration leg and a jog. The weather and daylight stops, faults and Stop are unchanged.
- **FR-005**: Every override bit is filtered by origin where the command is checked
  (`allowedOverrides`) and where it is kept (`MoveGuard::ran`), so no automation keeps one.
- **FR-006**: A person's Open or Close past an unconfirmed park writes a warning to the log and
  queues the information alert `check_overridden` (Normal; no setting of its own; held by the
  pause, the send mode and the master switch like any ordinary alert).
- **FR-007**: The move dialog: the red warning, **Open anyway** with the park named, **Wait for the
  park** only when the park is the only reason, Cancel after a sent Open stops it.
- **FR-008 (F31)**: An Alpaca and an MQTT Open refused for the weather carry the verdict's full
  text, SQMeter's reasons included, over HTTP and MQTT.

## Constitution rules relied on

- Who May Set a Check Aside (10.0.0, new): a person may, with a warning and a confirmation; an
  automation never, outside the named owner-decided exceptions.
- II. Close Only Onto a Parked Mount: a person's confirmed open (10.0.0); the interlock is
  re-checked throughout every other open and close.
- III. Unsafe Ends Closed: the weather and daylight still stop the open unless overridden; rain
  and doubt about the source can't be overridden.
- V: each rule has a native test (`test_person_overrides`, `test_weather_refusal_reasons`).

## Override audit (FR-005)

Every lock that can refuse or stop a move, by who asks. **Person** is an authenticated web UI or
REST request (with protection off, as for every setting, any LAN client is one: constitution VIII).
**Client** is an Alpaca client (N.I.N.A.); **MQTT** includes Home Assistant; **Automatic** is the
controller's own moves (the automatic close, the calibration run's and jog's own stops). "No" means
the lock holds whatever is sent; the code that decides is in brackets.

| Lock | Person | Client (Alpaca) | MQTT / HA | Automatic |
|---|---|---|---|---|
| Park, Open start (`mountNotParked`) | **Yes**, Open anyway, red warning (039) | No: waits and asks N.I.N.A. to park [`allowedOverrides`, `MoveGuard::admit`] | No: as Client | n/a: never opens |
| Park interlock during an open | **Yes**, for the confirmed open only (039) | No | No | n/a |
| Park, Close start | **Yes**, Close anyway (027); jog toward closed, warned (033) | No: waits | No: waits | Only the N.I.N.A.-lost close (030, owner option, off by default; fitted switches still count) |
| Park interlock during a close | **Yes**, confirmed close; jog toward closed | No | No | Only the N.I.N.A.-lost close (switches checked) |
| Weather: SQMeter's own unsafe (not rain), Open | **Yes**, Open anyway (027) | No | No | n/a |
| Weather: rain, rain sensor fault, a source missing/stale/unreachable/unreadable/offline, Open | **No** (F-1) | No | No | n/a |
| Weather stops an open under way | Only the reasons the person overrode | No | No | n/a |
| Daylight (`daylight`), Open | **Yes**, Open anyway (027) | No | No | n/a |
| Suspect park switch (`parkSwitchCheck`), Open | **No** (F-4) | No | No | n/a |
| Stop of the automatic close in unsafe weather or daylight | **Yes**, Stop stands it down (critical alert) | No, unless the owner's `autoClose.remoteStop` (014; counts as a person's stop, critical alert) (F-6) | As Client | n/a |
| Reversing a close (Open while closing or stopped closing) | **Yes** | No (`closing`) | No | n/a |
| Motion fault lockout | **Yes**, Clear fault | No (`notAPerson`) | No: no command for it | Only the Hall rail fault latched at rest clears itself after 60 s good (7.0.0) |
| Hall rail fault, moving | **Yes**, blind jog (033) | No | No | Only the blind close (030, `roof.blindClose`) |
| Position unknown, moving | **Yes**, slow, to the first magnet | No (`positionUnknown`) | No | Only the automatic close toward closed (8.0.0) |
| End magnets | **Yes**, blind jog with the magnets overridden (033) | No: can't jog | No | No |
| Jog toward open: weather, daylight, park, interlock | **No** (F-2) | No: can't jog | No | n/a |
| Calibration run: weather, daylight, park | **No** (F-3) | No | No | n/a |
| Supply low or unreadable, current sense unreadable | **No** (F-5) | No | No | No |
| Invalid settings or pins | No: fix the settings (not a judgement a person can make by looking) | No | No | No |
| Busy device (upload, restart, MQTT connect), setup hotspot | No: waits (the platform, not a safety check) | No | No | A close waits, then runs |
| A calibration run or a jog under way | Stop first | No (`calibrating`, `jogging`) | No | Ends them and closes |
| Test box bypasses | **Yes**, an authenticated settings save (022) | No | No | No |

No path found where an automation gets past a lock by sending something. One hardening: a close
deferred behind an upload, and the automatic close's blind close, handed their raw override bits
to `MoveGuard::ran`, which kept them unfiltered. Only a person's bits (already filtered) or the
automatic close's (only `OverrideNinaLost`) reached it, so nothing could exploit it, but the guard
now filters by origin itself and `test_the_guard_keeps_an_override_only_for_whom_it_is_allowed`
proves it.

The automatic exceptions above (N.I.N.A.-lost close, blind close, unknown-position close, Hall rail
self-clear, `autoClose.remoteStop`, test box) are each an earlier owner decision written into
Principles I-III; constitution 10.0.0 names them as the only ones.

### Follow-ups: locks a person can't get past yet

- **F-1** Open in rain, with a failed rain sensor or with the weather source in doubt. A person
  could see the sky on the camera, but the override would also have to hold back the automatic
  close for rain, which Principle III says must always act. Needs an owner decision on rain.
- **F-2** A jog toward open with the park not confirmed, or in weather or daylight that refuses an
  Open. Mirrors the jog toward closed (033): warned, hold to run. Needs the teach card's own
  confirmation.
- **F-3** The calibration run with the park not confirmed (or in weather/daylight). Needs a confirm
  on the calibration page and the leg's interlock set aside for the run.
- **F-4** The suspect park switch check (`parkSwitchCheck`), which disables Open outright. It only
  judges the park evidence, so a person's park override could set it aside too; the UI shows it as
  a disabled reason, not a blocker, so it needs the dialog wiring.
- **F-5** A low or unreadable 12 V supply, or current sense unreadable. Electrical protection the
  camera can't judge; likely to stay, listed for completeness.
- **F-6** `autoClose.remoteStop`: an owner setting (off by default) that lets an imaging app's or
  MQTT's Stop stand the automatic close down. Allowed by 10.0.0 as an owner-decided exception; the
  owner may want it removed under the new principle.

## Out of scope

The follow-ups above; a setting for the new alert's level.
