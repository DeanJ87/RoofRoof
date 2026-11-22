# Research: Bench Resilience

## D1. What a missed N.I.N.A. reply means during a close

- **Decision**: a reply that never came, couldn't be read or was an API error is the absence of
  evidence, not evidence of an unparked mount. During a move the interlock may ride on the last
  good reading while it is fresh (10 s). Any good reading that isn't parked replaces the memory and
  stops the move.
- **Why**: the bench showed a 3 s read timeout latching a fault mid-close. The poller already
  accepts a good reading for 10 s (five polls); a missed reply adds no age to the reading the close
  started on.
- **Rejected**: retrying the read faster (the request limit is 6 s; a retry can't make a reading
  fresher than the limit); a longer limit (weaker); counting consecutive failures (not bounded in
  time).

## D2. Where the bound is checked

- **Decision**: the loop decides `closeHold` (parked or riding, and `freshUntilMs` = the reading's
  `takenAtMs` + `ninaMaxAgeMs`). The control task compares its own clock (`SafetyPoller::nowMs`,
  the clock readings are stamped with) with `freshUntilMs` every step, for a good reading as well as
  a ride-through.
- **Why**: the loop evaluates once a second and hands over every 250 ms; checking in the step keeps
  the bound exact (to one step) whatever the loop does. Before, a good reading could be used for up
  to about 1.25 s past its limit.
- **Boundary**: fresh while `age <= limit`, as `ParkRule` already does.

## D3. Which failures ride

- **Decision**: `Unreachable`, `Unreadable`, `ApiError`. Not `NotConfigured` or `Waiting`: the
  poller sets those when the address is removed or changed, and the memory is dropped when the
  address generation changes.
- **Why**: a reading from another server proves nothing about this mount.

## D4. What the ride-through checks

- **Decision**: the remembered reading run through `evaluatePark` with the switches as read now, at
  the current time. Only a result of "parked" rides. The control task still checks every fitted
  switch live each step.
- **Why**: reuses the one park rule, so disagreement, connection and slewing mean the same thing.

## D5. Constitution wording

- **Decision**: PATCH 2.1.1. The start rule is unchanged. The re-check bullet adds that it uses
  the latest good reading while fresh, that a failed read doesn't count as a report of unparked,
  and that evidence past its freshness stops the move.
- **Risk stated**: with no park switches, a mount that unparks while N.I.N.A. stops answering can
  see the roof keep closing up to 10 s after the last good reading, about 1 s more than the first
  failed reply arrived before. Park switches still stop at once.

## D6. Partial saves

- **Finding**: `Config::applyJson` applies each key it finds and leaves every missing key as it
  was, section by section (verified for `{"park":{"nina":{"url":...}}}` on the bench). Unknown root
  keys are ignored, so `baseRevision` and `baseValues` don't reach the settings.
- **Decision**: the page sends the leaves that differ between its form and the settings as loaded
  (objects recursed, arrays and values compared whole), plus the wizard's `setup`.

## D7. Stale saves: revision plus loaded values

- **Decision**: `GET /api/config` sends `X-Config-Revision` (read before the settings, so a save
  in between can only make the page older, never newer). A save sends `baseRevision` and
  `baseValues`. Same revision: save. Otherwise compare each base value with the device's settings
  serialised as `GET /api/config` would (`toJson(true)`); differences are the conflicts.
- **Why**: field-level without a history on the device (no RAM between saves); a SQMeter sync or a
  change elsewhere never blocks an unrelated save. Numbers compare by value; a missing key equals
  `null`.
- **Rejected**: a whole-document revision check (every sync would block every open page); a list
  of changed paths per revision on the device (memory, and lost at a restart).

## D8. Credential partners

- **Decision**: when the patch touches any of a group, the base carries all of it:
  `wifi.{ssid,password}`, `mqtt.{broker,port,username,password}`, `auth.{username,password}`,
  `alerts.ntfy.{server,topic,token}`, `alerts.pushover.{userKey,appToken}`,
  `alerts.webhook.{url,authHeader}`.
- **Why**: a password typed for one network or server must never be saved against another.

## D9. The lock

- **Decision**: a new `UpdateConfigCallback` runs the check, the merge and the write under
  `configSaveLock` and bumps the revision inside it; the plain save is a wrapper of it.
- **Why**: before, the save merged onto a copy read outside the lock, so a Wi-Fi setup save on the
  loop could land between and be undone; and the bump after the lock let a second save see the old
  revision.

## D10. The revision across a restart

- **Decision**: seeded from `esp_random()` (31 bits) at start.
- **Why**: from 0, a page loaded before a restart could match a later revision by chance and skip
  the field check.
