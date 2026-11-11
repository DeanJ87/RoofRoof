# Research: Close on Unsafe

Each decision lists what was chosen, why, and what else was considered. The owner was not
available; open choices took the safest option the constitution allows (spec "Assumptions decided
for the owner").

## R1. Where the sequence runs

- **Decision**: a pure `CloseSeq::Sequence` in `lib/CloseSequenceLogic`, stepped on the main loop
  right after `SafetyInputs::handle()` and the hand-over to the roof (spec 003), every loop pass.
  The firmware glue (`src/CloseOnUnsafe.cpp`) gathers the inputs, carries out the decision and
  logs and alerts.
- **Rationale**: the verdict and park result live on the main loop (spec 004), `RoofControl` is
  safe to call from any task (spec 003), and alerts must be dispatched from the main loop
  (`AlertDispatcher::dispatch`, MQTT is published synchronously). The roof's own safety (gates,
  interlock, timeouts, watchdog) runs in the control task and doesn't depend on the loop: a stalled
  loop only delays the sequence, and spec 003 then treats the park as not confirmed (5 s hand-over
  limit).
- **Alternatives**: in the roof's control task (rejected: the task must never wait on the network
  or alerts, constitution I); its own task (rejected: one more stack and more locking for no gain).

## R2. Time base

- **Decision**: 64-bit milliseconds since start (`SafetyPoller::nowMs()`), as spec 004's records.
- **Rationale**: never wraps, so the "last park request" and "safe since" comparisons can't be
  fooled by the 49.7-day wrap of a 32-bit clock.

## R3. What counts as unsafe

- **Decision**: any verdict other than `Safe` (constitution III: doubt is unsafe). Unknown only
  occurs in spec 004's start-up window; treating it as unsafe means a restart with the roof open
  asks for a park until a source answers safe.
- **Alternatives**: waiting out the start-up window (rejected: it can last the full stale time,
  30 s by default, and would be a guess that the weather is fine).

## R4. Park confirmation

- **Decision**: the park counts as confirmed when spec 004's `ParkResult::parked` has held for 4 s
  without a break (`PARK_SETTLE_MS`). The close is then commanded with `Origin::Automatic` through
  `RoofControl::command`, whose gate checks the park at that moment and whose interlock re-checks it
  every step (spec 003).
- **Rationale**: N.I.N.A. is polled every 2 s and the park result is evaluated every second; two
  polls in a row filter a single good reading among failures, which would otherwise start a close
  the interlock would stop as a latched fault a second later (spec 003 known limit: one failed poll
  during a close stops it).
- **Alternatives**: no settle time (rejected: more interlock faults, each needing a person);
  longer (rejected: the roof stays open longer in the rain).

## R5. Park requests and rate limiting

- **Decision**: a request at the start of an attempt and every 30 s while waiting
  (`PARK_REQUEST_GAP_MS`), and never two within 30 s of each other, across attempts and weather
  changes. The spacing is kept in the sequence object for the whole uptime, not per attempt.
- **Rationale**: the poller sends one queued request before its next mount poll (spec 004); a
  request lost to a network blip is repeated. N.I.N.A. answers "already parked" harmlessly, but a
  verdict flapping every second must not turn into a request every second.

## R6. Time limits and what happens when they run out

| Limit | Value | On expiry |
|---|---|---|
| Park confirmed | `park.timeoutSeconds`, default 240 s (30-1800), from the first request of the attempt | "mount not parked" (critical), wait 5 min, then a new attempt |
| N.I.N.A. not configured | at once | the same |
| Back-off after a failed attempt | 5 min (`RETRY_AFTER_MS`) | a new attempt (a confirmed park still closes during the wait) |
| Waiting close (spec 003 `closePending`) | 30 s (`CLOSE_START_MS`) | stop, "roof didn't close" (too long), halted until closed or 10 min safe |
| After a refused or interrupted close | 5 min (`RETRY_AFTER_MS`), across episodes | no close before |
| Alerts | each event at most every 30 min (`REMIND_MS`); raised again every 30 min while the roof stays open | - |
| Close in motion | the move's own timeout + 30 s (`CLOSE_GUARD_SLACK_MS`), from the command | stop, "roof didn't close" (too long), halted until closed or 10 min safe |
| Position unknown before standing down | 2 s (`UNKNOWN_CONFIRM_MS`) | "roof can't close by itself" |
| Safe before a stood-down sequence re-arms | 10 min without a break (`REARM_SAFE_MS`) | back to idle |

- **Rationale for the park limit as a setting**: how long a mount takes to park (slew rate, how
  far it is from park) is a fact of the owner's mount, like the roof's travel time. 240 s covers a
  half-sky slew at slow rates; a shorter value alerts sooner.
- **Rationale for the guard**: the motion logic's own timeout (spec 002) is the real limit and faults
  first; the guard only catches a close that the motion logic somehow never ends.

## R7. A person's stop during the sequence

- **Decision**: the roof counts a person's stops and aborts in its snapshot (`RoofIo::Snapshot::personStops`, counted under its lock before the stop is applied);
  a change seen while the sequence is waiting or closing stands it down until the roof reads closed
  or the verdict has been safe for 10 minutes without a break. One critical alert.
- **Rationale**: a stop may come from someone at the roof (a cable caught, a hand near the gap);
  restarting the close automatically would override them. Constitution I wants every move to have
  a known reason, and the constitution's conflict rule is to stop and alert rather than guess. The
  cost is a roof left open after a mistaken stop, which the critical alert reports.
- **Alternatives**: respect the stop for a fixed time (5-30 min) then close (rejected: the person
  may still be there; there is no way to know); until the next unsafe episode (rejected: a flapping
  verdict would end the episode within seconds, so the 10-minute safe period is required).
- **Stops before the sequence**: only changes after the sequence left idle count; a stop pressed
  while it was safe has no effect on a later unsafe episode.

## R8. Unknown position and latched faults

- **Decision**: no park request and no command; one critical alert after 2 s of unknown (the Hall
  reading takes three steps to settle after a restart, so the first moments are always unknown).
  Once a person makes the position known again or clears the fault, the sequence resumes in the
  back-off wait (no request for 5 min; a confirmed park still closes). It ends on closed or 10 min of
  safe.
- **Rationale**: constitution I (never move by itself with the position unknown or a fault latched).
  Not even the mount is asked to park: with the roof in an unknown place the park slew could be the
  movement that hits it. Resuming after a person acted serves constitution III; the person has just
  issued an authenticated command and can stop the roof.

## R9. Weather clearing

- **Decision**: a safe verdict before the close is commanded ends the sequence (nothing reopened,
  N.I.N.A. is not asked to unpark); a close already commanded runs to the end.
- **Rationale**: stopping part-way leaves the roof neither open nor closed, which is worse than
  either if the weather turns again.

## R10. Alerts

- **Decision**: one new alert type `roof` (`Alerts::AlertType::Roof`) carrying the four critical
  events with fixed wording from the pure library (`CloseSeq::alertFor`), dispatched by
  `WebServer::raiseRoofAlert` at the `wake` level to every channel that is on, past the alert
  pause, the start-up grace and the master switch, to the channels that are switched on. Each event
  alerts at most once every 30 minutes, across episodes (a flapping verdict can't storm), and the
  episode's last critical event is raised again every 30 minutes while the roof stays open after
  it, because a delivery can fail (the dispatcher tries each channel once).
- **Rationale**: the brief keeps roof alert types, wording and settings in spec 007; one type with
  no setting is the smallest hook that reaches the existing channels, and 007 replaces it with its
  own events. The pause is the owner's choice for imaging-app alerts; a roof left open in the rain
  is not something to hold back; the same goes for the master switch, which on a new device is off
  while channels are being set up. The alert channels (Pushover, ntfy, webhook, MQTT) never go
  through N.I.N.A. or SQMeter, so losing them doesn't lose the alert (constitution III).
- **Alternatives**: reuse an existing type (rejected: they mean imaging-app events); add 007's full
  event list now (rejected: out of scope).

## R11. The `disagree` flag of the park result

- **Decision**: treated like any other "not parked" reason: the sequence waits, and the alert's text
  (from `Safety::describe`) says the sources disagree. The 004 user page said the close sequence
  would treat disagreement as a fault; it is corrected: nothing moves, so there is nothing to latch,
  and the critical alert covers it.

## R12. Flash

- **Decision**: no new strings beyond the log and alert texts; no status document changes. Expected
  growth a few KB, measured with `tools/firmware/flash_budget.py`.
