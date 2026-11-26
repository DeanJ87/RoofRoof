# Feature Specification: A Park Request Never Stops a Slew

**Feature Branch**: `spec/050-park-only-when-still` | **Created**: 2026-11-26 | **Constitution**: 12.0.0 -> 12.1.0 (II)

## Context

**Findings F57/F59 (the real roof and the real OnStep mount, twice on 2026-11-26).** RoofRoof asks
N.I.N.A. to park through the Advanced API's `GET /v2/api/equipment/mount/park`
(`Nina::ApiV2::parkRequest`, sent by `SafetyPoller::sendPark`). The plugin's handler stops a slewing
mount before it parks it (`if (mount.Slewing) mount.StopSlew(); mount.ParkTelescope()`), and a mount
that is already parking reads `Slewing` true. So:

1. N.I.N.A.'s sequence went unsafe and parked the mount itself;
2. a few seconds later RoofRoof's automatic close asked N.I.N.A. to park as well;
3. the plugin stopped the park part-way; OnStep, still decelerating, rejected the new park;
4. the mount was left neither parked nor slewing, and the roof (correctly) refused to close onto it.

N.I.N.A. logged "Mount has parked" after the failed park. RoofRoof didn't believe it: the park check
reads `AtPark` from `mount/info` (spec 004), which said not parked. That stays so.

The same can happen with RoofRoof's own requests: the automatic close and a command's park wait ask
again every park request gap (30 s by default), and a park that takes longer than that is stopped by
RoofRoof's own next request.

## User Scenarios & Testing

### User Story 1 - N.I.N.A. is already parking (P1)

Rain. N.I.N.A.'s sequence parks the mount; RoofRoof's automatic close wants the park 5 s later.

1. N.I.N.A. is read at once; the reading says the mount is slewing: nothing is sent. The log says
   "Not asking N.I.N.A. to park yet: the mount is slewing (waiting up to 180 s for it to stop)", and
   the park check's text (`parkText`, the dashboard's park row, the close's refusal) ends "waiting
   for the mount to stop slewing before asking N.I.N.A. to park".
2. N.I.N.A. is read every 2 s as usual. The park finishes: at park, not slewing. Nothing is sent
   ("The mount is already parked: no park request sent"), and the close goes ahead on the park.
3. The close sequence's next request (every gap) finds the mount parked and sends nothing.

### User Story 2 - The slew ends somewhere else (P1)

The mount was slewing to a target (a goto, a meridian flip) when the park was wanted. It stops, not
parked: RoofRoof sends the park, once.

### User Story 3 - RoofRoof's own park is under way (P1)

RoofRoof sent the park; the mount is parking. The close sequence's next request (30 s later) finds it
slewing and waits; the park finishes; nothing more is sent. A request within 10 s of RoofRoof's own
send is decided only on a reading taken 10 s or more after that send, so a mount that hasn't started
to read slewing yet is not asked twice.

### User Story 4 - A slew that doesn't end (P2)

The mount reads slewing for 3 minutes: a mount slewing that long is not parking. RoofRoof sends the
park anyway (the plugin then stops the slew and parks), with a warning in the log saying so.

### Edge cases

- The roof leaves its end during the wait (a person's jog): the request is dropped, nothing is sent
  (spec 043: a park is a slew, only with the roof at an end). The requester asks again, by its own
  rules, once the roof is back at an end.
- N.I.N.A. can't be read (no reply, an unreadable reply, an API error): sent as before, unless
  N.I.N.A.'s last good reading is still within its freshness (`NINA_FRESH_MS`, 10 s) and says
  slewing; then it waits like a fresh reading. The N.I.N.A.-lost close (specs 027, 030) asks for no
  park and is unchanged; so is everything spec 047 decides about moves on a stale park.
- N.I.N.A. answers but the mount is not connected: sent as before (N.I.N.A. refuses it, and the
  close's alerts say so).
- No N.I.N.A. address: as before (nothing to send; recorded as refused).
- A new N.I.N.A. address or API version: a waiting request is dropped, as a pending one was.
- Several requests while one waits (the close sequence and a command's park wait, each every gap):
  one wait, at most one send. The wait's limit counts from the first request.
- A requester that stops wanting the park during the wait (the weather turns safe): the waiting
  request is still sent when the slew ends, within the limit. Before, it was sent at once, stopping
  the slew, so nothing is worse; a requester has no way to withdraw a request today, and adding one
  isn't needed for this fix.

## Requirements

- **FR-001**: `Safety::ParkSender` (pure, `lib/SafetyInputLogic/ParkSend.h`) decides when a park
  request is sent, on the first N.I.N.A. reading taken at or after the request (and at least
  `PARK_SENT_SETTLE_MS`, 10 s, after RoofRoof's last send):
  - good, slewing: wait;
  - good, connected, at park, not slewing: done, nothing sent;
  - any other good reading: send once;
  - failed: wait while N.I.N.A.'s last good reading is within `NINA_FRESH_MS` and says slewing;
    otherwise send once (the behaviour before this spec).
- **FR-002**: A wait ends at `PARK_SLEW_WAIT_MS` (180 s from the first request) by sending the park,
  logged as a warning. It is below the park time limit's default (240 s), so a command's park wait
  and the automatic close can still see that park.
- **FR-003**: The roof is checked (`RoofIo::parkRequestAllowed`) where the request is decided (as
  before) and again at the moment of sending, after any wait: the main loop hands the poller
  `SafetyPoller::setMountMayMove` every pass. Not at an end drops the request, logged.
- **FR-004**: A request while one waits joins it: no second wait, no second send.
- **FR-005**: A request makes N.I.N.A. due at once (`PollSchedule::ninaWanted`), so the decision
  normally comes in the same pass; afterwards N.I.N.A. is read at its usual 2 s.
- **FR-006**: `Safety::ParkResult::askWaiting` (from the poller's snapshot) makes `Safety::describe`
  add "waiting for the mount to stop slewing before asking N.I.N.A. to park" to the not-parked text.
- **FR-007**: No new task, no lasting buffer: the sender is a few words of state in the poller. No
  new setting: the wait's limit is a constant (a setting would add UI needing a mockup review, and
  nothing yet calls for changing it).
- **FR-008**: Docs: close-on-unsafe (the park request), alpaca (the park wait), changelog;
  constitution 12.1.0.

## Constitution rules relied on

- II (12.1.0, new bullet): **a park request never stops a slew**. RoofRoof doesn't ask N.I.N.A. to
  park while a fresh reading says the mount is slewing (or already says it is parked); it waits, up
  to 180 s, then asks once. The park check is unchanged: only `AtPark` in a fresh reading (with the
  switches) confirms it, never N.I.N.A.'s answer to the request.
- II (11.1.0): a park is a slew, asked for only with the roof at an end, checked where it's decided
  and where it's sent (now after the wait too).
- V: the rule is pure and tested natively. VI: no new task or buffer on the poller task.

## Tests

`test_park_send`:

- slewing, then parked: nothing sent ("already parked");
- slewing, then stopped not parked: sent once;
- already at park: nothing sent;
- slewing through the limit: sent at the limit, as `SendAtLimit`;
- repeated requests while waiting: one send, the limit from the first;
- the roof leaves its end during the wait: nothing sent, the wait ends;
- a request just after RoofRoof's own send waits for a reading taken after the settle time;
- a reading taken before the request decides nothing;
- N.I.N.A. unreadable: sent when the last good reading is stale or not slewing; waits while it is
  fresh and slewing;
- not connected: sent; no address: sent at once (as before);
- the text: `describe` with `askWaiting`.

## Owner checks (pending)

- [ ] Bench or roof: park the mount from N.I.N.A., then make the weather unsafe within a few seconds:
  the log shows the wait and "already parked"; the mount parks; the roof closes.
- [ ] Start a goto in N.I.N.A. and make the weather unsafe: the park is sent once the goto ends.
- [ ] A slow park (OnStep): the close sequence's next request (30 s later) waits; one park in
  N.I.N.A.'s log.
