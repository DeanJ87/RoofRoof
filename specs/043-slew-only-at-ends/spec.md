# Feature Specification: The Telescope Moves Only With the Roof at an End

**Feature Branch**: `spec/043-slew-only-at-ends` | **Created**: 2026-11-26 | **Constitution**: 10.1.0 -> 10.2.0 (II, IV)

## Context

The owner's rule: the telescope is safe to move (any slew, a park slew and an unpark included) only
when the roof is at 0 % (the closed magnet) or 100 % (the open magnet). At any position in between,
the telescope hits the roof's beams. A collision is worse than rain.

**Finding F41.** RoofRoof asked N.I.N.A. to park without looking at the roof:

- the automatic close (`CloseSeq::Action::RequestPark` from `Sequence::requestIfDue`, sent by
  `CloseOnUnsafe::carryOut`), also from an unknown position (constitution 8.0.0) and before a blind
  close (spec 030);
- a command's park wait (spec 027: `MoveGuard`/`ParkWait`, sent by the main loop from
  `RoofControl::takeParkRequest`).

A roof stopped part-way (a person's Stop, a fault, a crash, a power cut, an unknown position) with
the mount unparked, then unsafe weather: the automatic close asked N.I.N.A. to park and the
telescope slewed under the part-open roof.

## User Scenarios & Testing

### User Story 1 - No park request between the ends (P1)

The roof is stopped at 42 % with the mount unparked when rain starts.

1. RoofRoof does not ask N.I.N.A. to park and does not close the roof (it would close onto an
   unparked mount).
2. The critical needs-person alert goes out at once: "Roof part-open (42 %) with the mount not
   parked: moving the roof or the telescope would hit the beams. A person must check." It is
   reminded every reminder interval while it holds.
3. A person parks the mount by hand: the close goes ahead as before, still asking N.I.N.A. nothing.
4. Or a person moves the roof to the open magnet: the park is asked for at once (no back-off,
   nothing failed), then the close as usual.
5. Or the weather turns safe: the sequence goes idle; unsafe again, it judges afresh.

The same with the position unknown (after a restart part-way, the Hall settled) and with a Hall rail
fault and a blind close ready: no park request; parked by a person, the slow or blind close runs.

### User Story 2 - A command's park wait (P1)

N.I.N.A. (or MQTT, or a person) sends Close with the roof stopped part-way and the mount unparked.
The command is refused for the park (`mountNotParked`) with the reason "... Roof part-open (42 %)
with the mount not parked: moving the roof or the telescope would hit the beams. A person must
check. N.I.N.A. was not asked to park" instead of waiting and asking. A person at the dashboard can
still confirm the close past the park (spec 039's path, warned). A wait already running ends,
refused, if the roof leaves its end (a person's jog).

### User Story 3 - N.I.N.A. can gate its own slews (P1)

A sequence reads the Alpaca Switch "Mount may move" (or the Dome's `ShutterStatus` == Open) before
Unpark, Slew, Center and a meridian flip, and after Open Dome Shutter loops until the roof is fully
open, with a timeout.

### User Story 4 - The dashboard says it once (P2)

The Roof card's **Telescope** row reads **May move** at either magnet and **Keep still** otherwise,
why behind its "?".

### Edge cases

- An opening roof (unsafe weather stops an open by itself) is not judged until it stops; nothing is
  asked meanwhile. A close under way, or waiting to start, is watched as before.
- The owner's N.I.N.A.-lost close (`autoClose.ninaLostClose`, off by default) asks for no park and
  goes ahead as before.
- A back-off left by a failed close still runs its course after the roof returns to an end.
- The controller not running: every switch reads false; the Dome reads Error.

## Requirements

- **FR-001**: One pure predicate in `lib/RoofIoLogic` (`MountMove.h`): `roofAtEnd`,
  `mountMayMove` and `parkRequestAllowed` are true only for `State::Closed` or `State::Open` at rest
  (not jogging, calibrating or driving).
- **FR-002**: The close sequence MUST NOT return `Action::RequestPark` unless `mountMayMove(roof)`.
- **FR-003**: With the roof not at an end (and not moving), the mount not parked and no
  N.I.N.A.-lost close, the sequence MUST enter `CannotMove` with `CannotClose` /
  `Cause::RoofBetweenEnds` (critical, `needs_person`), from idle and from a wait, and stay there,
  reminded, until the roof is at an end or the mount parked (then an attempt at once, or the
  back-off a failed close started) or nothing wants the roof closed (then idle).
- **FR-004**: With the mount parked, the closes Principle I allows (from an unknown position, blind)
  go ahead as before, without a park request.
- **FR-005**: A command's park wait MUST start only with the roof at an end, and end (refused) when
  the roof leaves it; otherwise the command is refused for the park with the text of FR-003.
- **FR-006**: Both senders check `parkRequestAllowed` again with the roof as it is when sending
  (`CloseOnUnsafe::carryOut`, the main loop's park wait request; the demo the same).
- **FR-007**: The status's `roof.mountMayMove`; the Roof card's Telescope row (DS-02, DS-04, DS-08).
- **FR-008**: An ASCOM Alpaca Switch, device 0, interface version 3, `UniqueID`
  `roofroof-<MAC>-switch-0`, listed after the Dome (which keeps index 0): read-only booleans
  `Mount may move`, `Roof fully open`, `Roof fully closed` in that fixed order. `CanWrite` and
  `CanAsync` false; `SetSwitch`, `SetSwitchValue`, `SetSwitchName`, `SetAsync`, `SetAsyncValue`,
  `CancelAsync` NotImplemented; an Id out of range InvalidValue for every member; a missing or
  malformed parameter HTTP 400; `DeviceState` with `GetSwitchN`, `GetSwitchValueN`,
  `StateChangeCompleteN` and `TimeStamp`. ConformU (conformance with the write tests on, and the
  protocol check) in CI with zero errors, issues and configuration alerts.
- **FR-009**: Docs: the hardening guide for N.I.N.A. sequences, the Switch, the close's new case,
  the alert, the status field.

## Options for exposing the rule to N.I.N.A.

| Option | For | Against |
|---|---|---|
| (a) An Alpaca Switch with read-only booleans | Says exactly the rule ("Mount may move"), including the closed end (where the Dome reads Closed, not Open); N.I.N.A.'s Switch instructions and Sequencer Powerups expressions can read it; ConformU-testable; costs nothing on the roof's side (read-only). | N.I.N.A. connects one Switch device at a time: an observatory with a power box on the Switch slot can't add it. |
| (b) Dome `ShutterStatus` == Open | Already there (spec 036: Open only at the open magnet), every Dome client reads it, no new device. | Says only "open end": a sequence that slews with the roof closed (a park at dusk) can't use it; Error covers both "part-open" and "faulted". |
| SafetyMonitor | One boolean N.I.N.A. already understands. | `IsSafe` means the weather to N.I.N.A.; reusing it would park and close on a roof stopped open, and confuse the user's real safety monitor. Rejected. |

**Chosen: (a) and (b).** The Switch is the primary gate; `ShutterStatus` == Open is documented as
the fallback when the Switch slot is taken. Both come from the same predicate.

## Constitution rules relied on

- I: position known only at a magnet or by the estimate; the automatic close's exceptions (unknown
  position, blind) stay, gated on the park.
- II: close only onto a parked mount (unchanged); **new (10.2.0)**: the telescope moves only with the
  roof at an end; RoofRoof never triggers a slew otherwise; a person keeps the confirmed paths.
- III: the steps of the unsafe sequence; step 2 (ask N.I.N.A. to park) now only at an end.
- IV: a second Alpaca device, read-only, ConformU in CI.
- Who May Set a Check Aside: an automation never gets past it; a person may, warned.

## Tests

- `test_mount_may_move`: the predicate for every state, jog, calibration and driving; the texts
  (percent bounded to 1-99, unknown); the switch values (and all false not running); the readings'
  fixed order and names; the sequence: stopped part-way / unknown, no request and the alert with
  reminders; faulted with a blind close ready; a request at the open magnet; a roof stopped while
  waiting stops the requests; back at an end, at once; parked while part-open, a close without a
  request; safe again idles; the N.I.N.A.-lost close unchanged; the alert's type, level and words.
- `test_move_guard`: no wait and no request unless at an end (every state, both commands, the
  percent text); a wait ends when the roof leaves its end.
- `test_alpaca_switch`: management listing and UniqueID, common members and interface version 3,
  every reading, Id out of range for every member, NotImplemented writes, HTTP 400 for bad
  parameters (and PUT casing), DeviceState, Alpaca off.
- `test_needs_person`: the banner's line names where the roof is.
- Updated for the new rule: `test_close_sequence`, `test_close_unknown` (W05 and the blind close),
  `test_close_standdown`, `test_close_daylight_weather`, `test_person_close_unknown`; the property
  tests' alert-throttle tables gain the new cause.
- ConformU 4.3.0 locally and 4.5.0 in CI against `tools/alpaca-sim`: Switch conformance and protocol
  with no errors, issues or alerts; the Dome unchanged.

## UI (DS rules)

One fact in one place (DS-08): a single **Telescope** row on the Roof card under **Mount park**,
built from the shared `PillRow` (no new styles, DS-01), the reason behind "?" (DS-04), the pill a
few words (DS-20). Not shown on firmware that doesn't report it. The needs-person banner and the
alert already carry the full sentence; the move dialog shows the refusal's own text.

## Assumptions decided for the owner

- The closed magnet counts as an end (the owner's 0 %): the switch is on, and a park may be asked
  for there by a command's wait (the roof is closed, the telescope under it clear of the beams).
- An opening roof is not alerted on until it stops (it stops by itself when unsafe), to avoid an
  alert for a few seconds of travel; nothing is asked meanwhile.
- A roof at an end with a fault latched (`State::Error`) is not "at an end": the fault means the
  position isn't trusted.
- The Switch is named **RoofRoof** like the Dome (different N.I.N.A. equipment slots), with its
  description saying what it is.
- Constitution amendment is MINOR (10.2.0): it adds a rule and a device, relaxes nothing.

## Follow-ups (not in this spec)

- A Home Assistant binary sensor for "Mount may move".
- RoofRoof can't stop N.I.N.A.'s own slews; the hardening guide is the mitigation.

## Owner checks (pending)

- [ ] Bench (bench mode, no motor): stop the roof part-way, N.I.N.A. unparked, unsafe weather: no
  park request in N.I.N.A.'s log, the needs-person alert arrives with the percent; park by hand, the
  close runs.
- [ ] N.I.N.A.: add the Switch, see the three switches change with the roof; a test sequence gated on
  `Mount may move` stops instead of slewing with the roof part-open.
