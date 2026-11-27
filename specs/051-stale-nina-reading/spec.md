# Feature Specification: N.I.N.A.'s Frozen Mount Values Are Not a Reading

**Feature Branch**: `spec/051-stale-nina-reading` | **Created**: 2026-11-27 | **Constitution**: 12.1.0 -> 12.2.0 (II)

## Context

**Finding F60 (the real rig, 2026-11-12 and 2026-11-17).** When the mount's USB serial link drops,
the ASCOM OnStep hub keeps `Connected` true on a dead COM port. N.I.N.A. then serves cached values
("GET Telescope.AtPark failed - Returning last known value True"), and the Advanced API's
`GET /v2/api/equipment/mount/info` returns `Connected: true` with `AtPark`, `Slewing`, `UTCDate`,
`SiderealTime`, `RightAscension` and the rest frozen. On 11-12 N.I.N.A. logged "Mount has parked"
after a park that failed on the dead port; on 11-17 it sat like that for 26 h.

RoofRoof takes the park from that reply (`Nina::ApiV2::parseMountInfo`, spec 004; the safety poller
reads it every 2 s; spec 050's `ParkSender` decides park requests on it). The danger: the port dies
just after an unpark, N.I.N.A.'s cache still says `AtPark` true while the mount is out, and RoofRoof
confirms a park and closes onto it.

## User Scenarios & Testing

### User Story 1 - The link dies with the mount parked (P1)

The roof is open, the mount parked. The USB cable is knocked out; the driver stays connected.

1. N.I.N.A. answers every 2 s with the same values: `AtPark` true, `SiderealTime` 5.2134.
2. 20 s after the clock last moved, the reading counts as unreadable. The log (warning, once):
   "N.I.N.A.'s mount values have stopped updating (mount connection stuck?): SiderealTime unchanged
   for 20 s while N.I.N.A. answers. Park unknown until it moves again". The park text: "not parked:
   N.I.N.A.'s mount values have stopped updating (mount connection stuck?), park unknown".
3. Rain: the automatic close can't confirm the park and does what it does when N.I.N.A. can't be
   read (specs 005, 027): asks N.I.N.A. to park (spec 050: as for an unreadable reply), keeps the
   roof open, raises the critical alert; after 60 s the N.I.N.A. not reachable alert; the owner's
   N.I.N.A.-lost close (spec 030), if on, counts the time.
4. The owner reconnects the mount; the clock moves: "N.I.N.A.'s mount values are updating again
   (SiderealTime moves): the park reading counts again". The park is confirmed from that reading.

### User Story 2 - The link dies just after an unpark (P1)

The mount is unparked; the port dies before N.I.N.A. saw it move. N.I.N.A.'s cache says parked. From
20 s on nothing is confirmed from it, so no open or close starts on it.

### User Story 3 - A move under way (P2)

A close started on a confirmed park; the link dies. It follows spec 047 exactly as if N.I.N.A. had
stopped answering: it goes on to its end on N.I.N.A.'s last answer having said parked (with the
switches as read now), and the "finished without a fresh park reading" report says so.

### Edge cases

- The clock across 0 h sidereal: a forward step across the wrap is a step.
- N.I.N.A. refreshes its values every few seconds (its device polling interval): the same value
  read twice in a row is normal; only 20 s without a change counts.
- N.I.N.A. restarted, or away for a while: good readings more than 20 s apart start the check again
  (a new baseline, not stale). A clock that steps back, or jumps ahead by more than the time that
  passed plus 10 s (a new clock: another mount, a restarted driver), starts it again too; its
  values change, so it is live.
- Failed reads in between (unreachable, unreadable) shorter than that gap don't hide a frozen clock.
- No `SiderealTime` in the reply (an older plugin, a driver that doesn't give it), a value that isn't
  a number from 0 to 24, or the mount not connected: no judgement; read as before.
- New N.I.N.A. settings: the check starts again.
- The first 20 s after the link dies: the frozen values still count (stated risk, constitution 12.2.0).
- `UTCDate` is not used: a date string to parse for no more evidence than `SiderealTime` gives.

## Requirements

- **FR-001**: `Nina::ApiV2::parseMountInfo` reads `Response.SiderealTime` (hours) into
  `MountReading::hasSiderealTime`/`siderealHours`, when it's a number from 0 to 24. The JSON filter
  gains the one field (filter document 192 -> 256 bytes on the stack; the parsed document unchanged
  at 512).
- **FR-002**: `Nina::MountClockWatch` (pure, `lib/NinaLogic/MountClock.h`) judges each reading in
  order. A good reading of a connected mount with a clock: unchanged for `MOUNT_CLOCK_STALE_MS`
  (20 s) since it last moved -> replaced by `failedReading(Unreadable, MOUNT_FROZEN_TEXT)` with
  `frozen` set. A change moves the baseline (live); a step back or ahead by more than the elapsed
  time plus `MOUNT_CLOCK_TOLERANCE_MS` (10 s) restarts it; good readings more than
  `MOUNT_CLOCK_GAP_MS` (20 s) apart restart it. Failed reads are not judged. It reports the change
  (`Frozen`, `Live`, `Unjudged`) once.
- **FR-003**: The safety poller applies it to every N.I.N.A. reading before storing it
  (`SafetyPoller::watchClock`, `src/SafetyPollerClock.cpp`), restarting it for a new N.I.N.A.
  generation, and logs each change once. Everything downstream (park rule, ride-through and spec
  047's unanswered hold, spec 050's sender, the N.I.N.A.-lost close and alert, the status and REST)
  sees an unreadable reading and needs no change.
- **FR-004**: `ParkResult::ninaFrozen`; `describe` words it "N.I.N.A.'s mount values have stopped
  updating (mount connection stuck?), park unknown" in place of "N.I.N.A. reply unreadable".
- **FR-005**: No new task, buffer, setting or alert: a few words of state in the poller; the
  existing `nina_lost` alert carries the text.
- **FR-006**: Docs: safety-inputs (a new section), close-on-unsafe (spec 047 and 050 sections),
  REST (`nina.text`), changelog, roadmap; constitution 12.2.0.

## Constitution rules relied on

- II (12.2.0, new bullet): **a frozen reading is not a reading**; with the stated 20 s risk.
- II (12.0.0): a move under way goes on when N.I.N.A. merely stops answering; a frozen reading is
  that.
- II (12.1.0): the park request decides an unreadable reading as before; never "already parked".
- V: the rule is pure and tested natively. VI: no new task or buffer.

## Tests

`test_mount_clock`: an advancing clock is live (also one that steps every 5 s); frozen 18 s still
good, 20 s unreadable with the text, logged once; a frozen `AtPark` true never confirms the park (nor
with both switches closed), counts as N.I.N.A. lost; a move under way: unanswered (spec 047) on a
frozen "parked", not on a frozen "not at park"; recovery when the clock moves, and freezing again;
the 24 h wrap; a gap restarts the baseline, a step back or a jump is live and the new baseline;
failed reads shorter than the gap don't hide it; new settings; no clock or not connected: as before;
`ParkSender` with a frozen reading: sent (never `AlreadyParked`); frozen while slewing: waits while
the last good reading is fresh, then sent.

`test_nina`: `SiderealTime` parsed; missing; a whole number; text, negative and past 24 h ignored.

## Owner checks (pending)

- [ ] Bench or rig: with N.I.N.A. connected to the mount, pull the mount's USB cable (the driver
  stays connected): within about 20 s the log shows the warning and the park row the frozen text;
  plug it back and reconnect: "updating again", the park counts again.
- [ ] A N.I.N.A. with a long device polling interval (5 s): no false "stopped updating".
